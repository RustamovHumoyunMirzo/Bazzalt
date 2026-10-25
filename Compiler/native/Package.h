#pragma once
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <array>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>
#include <ryml.hpp>

extern "C" size_t ZSTD_decompress(void*,size_t,const void*,size_t);
extern "C" unsigned ZSTD_isError(size_t);
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
        Require(base!=L"CON"&&base!=L"PRN"&&base!=L"AUX"&&base!=L"NUL"&&base!=L"CONIN$"&&base!=L"CONOUT$"&&!(base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&((base[3]>=L'0'&&base[3]<=L'9')||base[3]==wchar_t(0x00b9)||base[3]==wchar_t(0x00b2)||base[3]==wchar_t(0x00b3))),"Reserved package path");}
    return path;
}
struct Algorithm {BCRYPT_ALG_HANDLE Value{};~Algorithm(){if(Value)BCryptCloseAlgorithmProvider(Value,0);}};
struct Key {BCRYPT_KEY_HANDLE Value{};~Key(){if(Value)BCryptDestroyKey(Value);}};
struct WipeBytes {std::vector<unsigned char>& Value;~WipeBytes(){if(!Value.empty())SecureZeroMemory(Value.data(),Value.size());}};
struct Decompressor {DECOMPRESSOR_HANDLE Value{};~Decompressor(){if(Value)CloseDecompressor(Value);}};
inline std::array<unsigned char,32> Hash(const std::vector<unsigned char>& data,const std::vector<unsigned char>& key={}){
    Algorithm alg;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_SHA256_ALGORITHM,nullptr,key.empty()?0:BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0,"SHA-256 unavailable");
    std::array<unsigned char,32> digest{};Require(BCryptHash(alg.Value,key.empty()?nullptr:const_cast<PUCHAR>(key.data()),ULONG(key.size()),const_cast<PUCHAR>(data.data()),ULONG(data.size()),digest.data(),ULONG(digest.size()))>=0,"Hash failed");return digest;
}
inline std::vector<unsigned char> HexKey(const std::wstring& value){
    Require(value.size()==64,"Environment key must contain 64 hex characters");std::vector<unsigned char> key;
    auto digit=[](wchar_t c){if(c>='0'&&c<='9')return int(c-'0');if(c>='a'&&c<='f')return int(c-'a'+10);if(c>='A'&&c<='F')return int(c-'A'+10);throw std::runtime_error("Invalid key encoding");};
    for(size_t i=0;i<64;i+=2)key.push_back(static_cast<unsigned char>(digit(value[i])*16+digit(value[i+1])));return key;
}
inline std::uint64_t PlayerSealOffset(const std::filesystem::path& executable){
    // Authenticode appends a certificate table after the original PE overlay.
    // Its directory entry uses a file offset, not an RVA. Never scan arbitrary
    // executable bytes for a key: only accept the bounded pre-certificate tail.
    std::ifstream file(executable,std::ios::binary);Require(bool(file),"Could not read player seal");
    const auto length=std::filesystem::file_size(executable);std::uint64_t end=length;
    IMAGE_DOS_HEADER dos{};file.read(reinterpret_cast<char*>(&dos),sizeof(dos));Require(bool(file)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<=1024*1024,"Invalid player PE header");
    file.seekg(dos.e_lfanew);DWORD signature=0;IMAGE_FILE_HEADER header{};file.read(reinterpret_cast<char*>(&signature),sizeof(signature));file.read(reinterpret_cast<char*>(&header),sizeof(header));
    Require(bool(file)&&signature==IMAGE_NT_SIGNATURE&&header.SizeOfOptionalHeader>=144&&header.SizeOfOptionalHeader<=4096,"Invalid player optional header");
    std::vector<unsigned char> optional(header.SizeOfOptionalHeader);file.read(reinterpret_cast<char*>(optional.data()),optional.size());Require(bool(file),"Truncated player optional header");
    WORD magic=0;std::memcpy(&magic,optional.data(),2);Require(magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC||magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"Unsupported player PE format");
    const size_t directories=magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC?96:112;Require(optional.size()>=directories+40,"Missing player certificate directory");
    IMAGE_DATA_DIRECTORY certificate{};std::memcpy(&certificate,optional.data()+directories+IMAGE_DIRECTORY_ENTRY_SECURITY*sizeof(IMAGE_DATA_DIRECTORY),sizeof(certificate));
    if(certificate.VirtualAddress||certificate.Size){Require(certificate.VirtualAddress>=std::uint64_t(dos.e_lfanew)+sizeof(DWORD)+sizeof(header)+optional.size()&&certificate.Size>=8&&std::uint64_t(certificate.VirtualAddress)+certificate.Size==length,"Invalid player certificate table");end=certificate.VirtualAddress;}
    for(std::uint64_t padding=0;padding<=7&&end>=112+padding;++padding){
        file.clear();file.seekg(std::streamoff(end-112-padding));std::array<char,8> marker{};file.read(marker.data(),marker.size());
        if(file&&(std::string(marker.data(),8)=="BZSIG002"||std::string(marker.data(),8)=="BZSIG003")){
            file.seekg(std::streamoff(end-padding));bool zeros=true;for(std::uint64_t i=0;i<padding;++i)if(file.get()!=0)zeros=false;
            if(zeros)return end-112-padding;
        }
    }
    throw std::runtime_error("Missing game content signature seal; rebuild the game");
}
template<class T> inline T Number(std::istream& f){T value{};f.read(reinterpret_cast<char*>(&value),sizeof(value));Require(bool(f),"Truncated package");return value;}
inline void Bytes(std::istream& f,void* data,size_t size){f.read(static_cast<char*>(data),std::streamsize(size));Require(bool(f),"Truncated package payload");}
inline void Extract(const std::filesystem::path& folder,const std::filesystem::path& output,const std::vector<std::string>& packages,const std::vector<unsigned char>& key){
    struct Progress{std::uint64_t Offset=0,Total=0;};std::map<std::string,Progress> progress;std::uint64_t allocated=0;size_t records=0;
    Algorithm alg;Key aes;Decompressor codec;
    std::vector<unsigned char> digestKey;WipeBytes wipeDigestKey{digestKey};
    Require(CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&codec.Value)!=0,"Package decompressor unavailable");
    if(!key.empty()){
        Require(key.size()==32&&BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_AES_ALGORITHM,nullptr,0)>=0,"AES unavailable");
        Require(BCryptSetProperty(alg.Value,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0,"GCM unavailable");
        Require(BCryptGenerateSymmetricKey(alg.Value,&aes.Value,nullptr,0,const_cast<PUCHAR>(key.data()),ULONG(key.size()),0)>=0,"Could not initialize encryption key");
        const std::string purpose="Bazzalt/v3/segment-digest";auto derived=Hash(std::vector<unsigned char>(purpose.begin(),purpose.end()),key);digestKey.assign(derived.begin(),derived.end());SecureZeroMemory(derived.data(),derived.size());
    }
    for(const auto& filename:packages){
        auto relative=SafePath(filename);Require(relative.extension()==".bpak"&&!relative.has_parent_path(),"Invalid chunk filename");std::ifstream f(folder/relative,std::ios::binary);std::array<char,8> magic{};Bytes(f,magic.data(),8);const auto version=std::string(magic.data(),8);const bool version3=version=="BZPAK003",modern=version3||version=="BZPAK002";Require(modern||version=="BZPAK001","Unsupported package version");
        while(f.peek()!=std::char_traits<char>::eof()){
            Require(++records<=1000000,"Package record limit exceeded");std::vector<unsigned char> header(modern?29:25);Bytes(f,header.data(),header.size());
            auto u32=[&](int i){std::uint32_t v;std::memcpy(&v,header.data()+i,4);return v;};auto u64=[&](int i){std::uint64_t v;std::memcpy(&v,header.data()+i,8);return v;};
            auto names=u32(0),size=u32(4),rawSize=modern?u32(8):size;auto offset=u64(modern?12:8),total=u64(modern?20:16);auto flags=header[modern?28:24];const bool encrypted=flags&1,compressed=flags&2,alias=flags&4;
            Require(names>0&&names<=4096u+(version3&&encrypted?28u:0u)&&size<=4u*1024u*1024u&&rawSize<=4u*1024u*1024u&&total<=8ull*1024*1024*1024&&offset<=total&&flags<=(modern?15:1)&&(!(flags&8)||compressed)&&encrypted==!key.empty(),"Invalid package record");
            Require(alias?(!compressed&&offset==0&&rawSize==0&&size>0&&size<=4096):(rawSize<=total-offset&&(rawSize>0||total==0)&&(!compressed?size==rawSize:size>0)),"Invalid package payload framing");
            std::string encodedName(names,'\0');Bytes(f,encodedName.data(),names);std::string name=encodedName;
            if(version3&&encrypted){
                Require(names>28,"Invalid encrypted package path");const auto count=names-28;std::vector<unsigned char> decoded(count);ULONG written=0;
                BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);info.pbNonce=reinterpret_cast<PUCHAR>(encodedName.data());info.cbNonce=12;info.pbTag=reinterpret_cast<PUCHAR>(encodedName.data()+names-16);info.cbTag=16;info.pbAuthData=header.data();info.cbAuthData=ULONG(header.size());
                Require(BCryptDecrypt(aes.Value,reinterpret_cast<PUCHAR>(encodedName.data()+12),count,&info,nullptr,0,decoded.data(),count,&written,0)>=0&&written==count,"Package path authentication failed");name.assign(decoded.begin(),decoded.end());SecureZeroMemory(decoded.data(),decoded.size());
            }
            auto path=SafePath(name);
            // Case-insensitive filesystem aliases must not overwrite a previous entry.
            auto identity=path.wstring();CharLowerBuffW(identity.data(),DWORD(identity.size()));auto utf8=std::filesystem::path(identity).u8string();std::string canonical(reinterpret_cast<const char*>(utf8.data()),utf8.size());
            auto [found,inserted]=progress.try_emplace(canonical);auto& item=found->second;
            if(inserted){allocated+=total;Require(allocated<=32ull*1024*1024*1024,"Package extraction limit exceeded");item.Total=total;Require(!std::filesystem::exists(output/path),"Duplicate package path");}
            else Require(item.Offset<item.Total,"Duplicate package file");
            Require(item.Offset==offset&&item.Total==total,"Out-of-order package segment");
            std::array<unsigned char,12> nonce{};std::array<unsigned char,16> tag{};std::array<unsigned char,32> digest{};Bytes(f,nonce.data(),12);Bytes(f,tag.data(),16);Bytes(f,digest.data(),32);std::vector<unsigned char> data(size);Bytes(f,data.data(),size);
            if(encrypted){
                auto aad=std::vector<unsigned char>(header.begin(),header.end());aad.insert(aad.end(),encodedName.begin(),encodedName.end());
                BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);info.pbNonce=nonce.data();info.cbNonce=12;info.pbTag=tag.data();info.cbTag=16;info.pbAuthData=aad.data();info.cbAuthData=ULONG(aad.size());
                std::vector<unsigned char> plain(size?size:1);ULONG count=0;Require(BCryptDecrypt(aes.Value,data.data(),size,&info,nullptr,0,plain.data(),ULONG(plain.size()),&count,0)>=0&&count==size,"Package authentication failed");plain.resize(size);data=std::move(plain);
            }
            Require((version3&&encrypted?Hash(data,digestKey):Hash(data))==digest,"Package checksum mismatch");std::filesystem::create_directories((output/path).parent_path());
            if(alias){
                auto target=SafePath(std::string(data.begin(),data.end()));auto identity=target.wstring();CharLowerBuffW(identity.data(),DWORD(identity.size()));auto utf8=std::filesystem::path(identity).u8string();std::string canonical(reinterpret_cast<const char*>(utf8.data()),utf8.size());
                const auto previous=progress.find(canonical);Require(previous!=progress.end()&&previous!=found&&previous->second.Offset==total&&previous->second.Total==total,"Invalid package alias");
                Require(std::filesystem::is_regular_file(output/target)&&std::filesystem::file_size(output/target)==total,"Missing package alias target");
                std::filesystem::copy_file(output/target,output/path);item.Offset=total;continue;
            }
            if(compressed){std::vector<unsigned char> plain(rawSize);SIZE_T count=0;if(flags&8){count=ZSTD_decompress(plain.data(),plain.size(),data.data(),data.size());Require(!ZSTD_isError(count)&&count==rawSize,"Invalid Zstd package payload");}else Require(Decompress(codec.Value,data.data(),data.size(),plain.data(),plain.size(),&count)!=0&&count==rawSize,"Invalid compressed package payload");data=std::move(plain);}
            std::ofstream file(output/path,std::ios::binary|(offset?std::ios::app:std::ios::trunc));file.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));Require(bool(file),"Could not extract game content");item.Offset+=rawSize;
        }
    }
    for(const auto& [_,item]:progress)Require(item.Offset==item.Total,"Incomplete game package");
}
}
