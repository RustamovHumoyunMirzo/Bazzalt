#pragma once
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <array>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>
#include <ryml.hpp>

namespace Bazzalt::Game {
inline void Require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
inline std::string Text(ryml::ConstNodeRef n){auto v=n.val();return {v.str,v.len};}
inline std::string Read(const std::filesystem::path& path,std::uintmax_t limit=16*1024*1024){Require(std::filesystem::file_size(path)<=limit,"File exceeds size limit");std::ifstream f(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{});Require(bool(f)||f.eof(),"Could not read file");return text;}
inline std::filesystem::path SafePath(const std::string& name){
    for(unsigned char c:name)Require(c>=32,"Control character in package path");
    Require(!name.empty()&&name.size()<=4096&&name.find("//")==std::string::npos&&name.back()!='/'&&name.find_first_of("\\:\0",0,3)==std::string::npos,"Invalid package path");
    auto path=std::filesystem::u8path(name);Require(!path.is_absolute()&&!path.has_root_name(),"Absolute package path");
    for(auto part:path){auto text=part.wstring();Require(!text.empty()&&text!=L"."&&text!=L".."&&text.back()!=L'.'&&text.back()!=L' '&&text.find_first_of(L"<>\"|?*")==std::wstring::npos,"Unsafe package path");
        auto base=text.substr(0,text.find(L'.'));CharUpperBuffW(base.data(),DWORD(base.size()));
        Require(base!=L"CON"&&base!=L"PRN"&&base!=L"AUX"&&base!=L"NUL"&&!(base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&base[3]>=L'1'&&base[3]<=L'9'),"Reserved package path");}
    return path;
}
struct Algorithm {BCRYPT_ALG_HANDLE Value{};~Algorithm(){if(Value)BCryptCloseAlgorithmProvider(Value,0);}};
struct Key {BCRYPT_KEY_HANDLE Value{};~Key(){if(Value)BCryptDestroyKey(Value);}};
inline std::array<unsigned char,32> Hash(const std::vector<unsigned char>& data){
    Algorithm alg;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA-256 unavailable");
    std::array<unsigned char,32> digest{};Require(BCryptHash(alg.Value,nullptr,0,const_cast<PUCHAR>(data.data()),ULONG(data.size()),digest.data(),ULONG(digest.size()))>=0,"Hash failed");return digest;
}
inline std::vector<unsigned char> HexKey(const std::wstring& value){
    Require(value.size()==64,"Environment key must contain 64 hex characters");std::vector<unsigned char> key;
    auto digit=[](wchar_t c){if(c>='0'&&c<='9')return int(c-'0');if(c>='a'&&c<='f')return int(c-'a'+10);if(c>='A'&&c<='F')return int(c-'A'+10);throw std::runtime_error("Invalid key encoding");};
    for(size_t i=0;i<64;i+=2)key.push_back(static_cast<unsigned char>(digit(value[i])*16+digit(value[i+1])));return key;
}
inline std::vector<unsigned char> LoadKey(ryml::ConstNodeRef config,const std::filesystem::path& executable){
    if(Text(config["encryption"])=="none")return {};
    const auto method=Text(config["key_storage"]);
    if(method=="file"){auto text=Read(executable.parent_path()/"game.key",32);Require(text.size()==32,"Invalid key file");return {text.begin(),text.end()};}
    if(method=="environment"){
        auto name=std::filesystem::u8path(Text(config["key_environment"])).wstring();std::array<wchar_t,66> buffer{};
        auto n=GetEnvironmentVariableW(name.c_str(),buffer.data(),DWORD(buffer.size()));Require(n==64,"Game encryption key is not available in the environment");return HexKey(buffer.data());
    }
    Require(method=="embedded","Unsupported key storage");std::ifstream f(executable,std::ios::binary);f.seekg(-40,std::ios::end);std::array<char,8> magic{};std::vector<unsigned char> key(32);f.read(magic.data(),8);f.read(reinterpret_cast<char*>(key.data()),32);Require(bool(f)&&std::string(magic.data(),8)=="BZKEY001","Missing embedded key");return key;
}
template<class T> inline T Number(std::istream& f){T value{};f.read(reinterpret_cast<char*>(&value),sizeof(value));Require(bool(f),"Truncated package");return value;}
inline void Bytes(std::istream& f,void* data,size_t size){f.read(static_cast<char*>(data),std::streamsize(size));Require(bool(f),"Truncated package payload");}
inline void Extract(const std::filesystem::path& folder,const std::filesystem::path& output,const std::vector<std::string>& packages,const std::vector<unsigned char>& key){
    struct Progress{std::uint64_t Offset=0,Total=0;};std::map<std::string,Progress> progress;std::uint64_t allocated=0;size_t records=0;
    Algorithm alg;Key aes;
    if(!key.empty()){
        Require(key.size()==32&&BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_AES_ALGORITHM,nullptr,0)>=0,"AES unavailable");
        Require(BCryptSetProperty(alg.Value,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0,"GCM unavailable");
        Require(BCryptGenerateSymmetricKey(alg.Value,&aes.Value,nullptr,0,const_cast<PUCHAR>(key.data()),ULONG(key.size()),0)>=0,"Could not initialize encryption key");
    }
    for(const auto& filename:packages){
        auto relative=SafePath(filename);Require(relative.extension()==".bpak"&&!relative.has_parent_path(),"Invalid chunk filename");std::ifstream f(folder/relative,std::ios::binary);std::array<char,8> magic{};Bytes(f,magic.data(),8);Require(std::string(magic.data(),8)=="BZPAK001","Unsupported package version");
        while(f.peek()!=std::char_traits<char>::eof()){
            Require(++records<=1000000,"Package record limit exceeded");std::array<unsigned char,25> header{};Bytes(f,header.data(),header.size());
            auto u32=[&](int i){std::uint32_t v;std::memcpy(&v,header.data()+i,4);return v;};auto u64=[&](int i){std::uint64_t v;std::memcpy(&v,header.data()+i,8);return v;};
            auto names=u32(0),size=u32(4);auto offset=u64(8),total=u64(16);auto encrypted=header[24];
            Require(names>0&&names<=4096&&size<=4u*1024u*1024u&&total<=8ull*1024*1024*1024&&offset<=total&&size<=total-offset&&(size>0||total==0)&&encrypted<=1&&bool(encrypted)==!key.empty(),"Invalid package record");
            std::string name(names,'\0');Bytes(f,name.data(),names);auto path=SafePath(name);
            // Case-insensitive filesystem aliases must not overwrite a previous entry.
            auto identity=path.wstring();CharLowerBuffW(identity.data(),DWORD(identity.size()));auto utf8=std::filesystem::path(identity).u8string();std::string canonical(reinterpret_cast<const char*>(utf8.data()),utf8.size());
            auto [found,inserted]=progress.try_emplace(canonical);auto& item=found->second;
            if(inserted){allocated+=total;Require(allocated<=32ull*1024*1024*1024,"Package extraction limit exceeded");item.Total=total;Require(!std::filesystem::exists(output/path),"Duplicate package path");}
            else Require(item.Offset<item.Total,"Duplicate package file");
            Require(item.Offset==offset&&item.Total==total,"Out-of-order package segment");
            std::array<unsigned char,12> nonce{};std::array<unsigned char,16> tag{};std::array<unsigned char,32> digest{};Bytes(f,nonce.data(),12);Bytes(f,tag.data(),16);Bytes(f,digest.data(),32);std::vector<unsigned char> data(size);Bytes(f,data.data(),size);
            if(encrypted){
                auto aad=std::vector<unsigned char>(header.begin(),header.end());aad.insert(aad.end(),name.begin(),name.end());
                BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);info.pbNonce=nonce.data();info.cbNonce=12;info.pbTag=tag.data();info.cbTag=16;info.pbAuthData=aad.data();info.cbAuthData=ULONG(aad.size());
                std::vector<unsigned char> plain(size?size:1);ULONG count=0;Require(BCryptDecrypt(aes.Value,data.data(),size,&info,nullptr,0,plain.data(),ULONG(plain.size()),&count,0)>=0&&count==size,"Package authentication failed");plain.resize(size);data=std::move(plain);
            }
            Require(Hash(data)==digest,"Package checksum mismatch");std::filesystem::create_directories((output/path).parent_path());
            std::ofstream file(output/path,std::ios::binary|(offset?std::ios::app:std::ios::trunc));file.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));Require(bool(file),"Could not extract game content");item.Offset+=size;
        }
    }
    for(const auto& [_,item]:progress)Require(item.Offset==item.Total,"Incomplete game package");
}
}
