#pragma once
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <cstdlib>
#include <openssl/ecdsa.h>
#include <openssl/crypto.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>
#include <map>
#include <memory>
#include <set>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <ryml.hpp>

namespace Bazzalt::Game {
inline void Require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
inline std::string Text(ryml::ConstNodeRef node){auto value=node.val();return {value.str,value.len};}
inline void SecureZeroMemory(void* data,std::size_t size){OPENSSL_cleanse(data,size);}
struct WipeBytes {std::vector<unsigned char>& Value;~WipeBytes(){if(!Value.empty())SecureZeroMemory(Value.data(),Value.size());}};
template<class T> inline T Number(std::istream& file){T value{};file.read(reinterpret_cast<char*>(&value),sizeof(value));Require(bool(file),"Truncated package");return value;}
inline void Bytes(std::istream& file,void* data,std::size_t size){file.read(static_cast<char*>(data),std::streamsize(size));Require(bool(file),"Truncated package payload");}
inline std::string Read(const std::filesystem::path& path,std::uintmax_t limit=16*1024*1024){Require(std::filesystem::file_size(path)<=limit,"File exceeds size limit");std::ifstream file(path,std::ios::binary);Require(bool(file),"Could not open file");return {(std::istreambuf_iterator<char>(file)),{}};}
inline std::filesystem::path SafePath(const std::string& name){
    Require(!name.empty()&&name.size()<=4096&&name.front()!='/'&&name.back()!='/'&&name.find("//")==std::string::npos,"Invalid package path");
    for(unsigned char c:name)Require(c>=32&&c!='\\'&&c!=':',"Invalid package path");auto path=std::filesystem::u8path(name);Require(!path.has_root_path(),"Absolute package path");
    for(const auto& part:path){const auto text=part.string();Require(text!="."&&text!=".."&&text.back()!='.'&&text.back()!=' '&&text.find_first_of("<>\"|?*")==std::string::npos,"Unsafe package path");}return path;
}
inline std::array<unsigned char,32> Hash(const std::vector<unsigned char>& data,const std::vector<unsigned char>& key={}){
    std::array<unsigned char,32> result{};unsigned count=0;
    if(key.empty())Require(EVP_Digest(data.data(),data.size(),result.data(),&count,EVP_sha256(),nullptr)==1&&count==32,"SHA-256 failed");
    else Require(HMAC(EVP_sha256(),key.data(),int(key.size()),data.data(),data.size(),result.data(),&count)&&count==32,"HMAC failed");return result;
}
inline std::vector<unsigned char> DecodeHex(const std::string& text,std::size_t expected=0){
    Require(text.size()%2==0&&text.size()<=32768&&(!expected||text.size()==expected*2),"Invalid security field size");const auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("Invalid hex encoding");};std::vector<unsigned char> result;for(std::size_t i=0;i<text.size();i+=2)result.push_back(static_cast<unsigned char>(digit(text[i])*16+digit(text[i+1])));return result;
}
inline std::vector<unsigned char> Derive(const std::vector<unsigned char>& key,const std::vector<unsigned char>& salt,const std::string& purpose){auto digest=Hash(key,salt);std::vector<unsigned char> prk(digest.begin(),digest.end());WipeBytes wipe{prk};SecureZeroMemory(digest.data(),digest.size());const auto info="Bazzalt/v3/"+purpose;std::vector<unsigned char> input(info.begin(),info.end());input.push_back(1);auto result=Hash(input,prk);return {result.begin(),result.end()};}
inline std::vector<unsigned char> DecryptGcm(const std::vector<unsigned char>& key,const unsigned char* data,std::size_t size,const unsigned char* nonce,const unsigned char* tag,const std::vector<unsigned char>& aad){
    Require(key.size()==32&&size<=64*1024*1024&&aad.size()<=64*1024*1024,"Invalid GCM size");std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);Require(bool(context),"AES context unavailable");int count=0,total=0;std::vector<unsigned char> plain(size+16);
    Require(EVP_DecryptInit_ex(context.get(),EVP_aes_256_gcm(),nullptr,nullptr,nullptr)==1&&EVP_CIPHER_CTX_ctrl(context.get(),EVP_CTRL_GCM_SET_IVLEN,12,nullptr)==1&&EVP_DecryptInit_ex(context.get(),nullptr,nullptr,key.data(),nonce)==1,"AES initialization failed");
    if(!aad.empty())Require(EVP_DecryptUpdate(context.get(),nullptr,&count,aad.data(),int(aad.size()))==1,"GCM AAD failed");
    Require(EVP_DecryptUpdate(context.get(),plain.data(),&count,data,int(size))==1,"GCM decryption failed");total=count;Require(EVP_CIPHER_CTX_ctrl(context.get(),EVP_CTRL_GCM_SET_TAG,16,const_cast<unsigned char*>(tag))==1,"GCM tag failed");
    if(EVP_DecryptFinal_ex(context.get(),plain.data()+total,&count)!=1){SecureZeroMemory(plain.data(),plain.size());throw std::runtime_error("Game authentication failed");}plain.resize(total+count);Require(plain.size()==size,"Invalid GCM length");return plain;
}
inline std::vector<unsigned char> DecryptEnvelope(const std::vector<unsigned char>& key,const std::vector<unsigned char>& data,const std::vector<unsigned char>& aad){Require(data.size()>=28,"Truncated encrypted envelope");return DecryptGcm(key,data.data()+12,data.size()-28,data.data(),data.data()+data.size()-16,aad);}
inline std::uint64_t PlayerSealOffset(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);const auto size=std::filesystem::file_size(path);Require(size>=132,"Missing player seal");std::array<char,4> elf{};Bytes(file,elf.data(),4);Require(std::string(elf.data(),4)==std::string("\x7f" "ELF",4),"Invalid ELF player");file.seekg(std::streamoff(size-112));std::array<char,8> marker{};Bytes(file,marker.data(),8);Require(std::string(marker.data(),8)=="BZSIG003","Missing player seal");return size-112;}
inline std::vector<unsigned char> LoadRootKey(ryml::ConstNodeRef boot,const std::filesystem::path& executable){
    if(Text(boot["content_encrypted"])=="false"){Require(Text(boot["manifests_encrypted"])=="false"&&Text(boot["key_storage"])=="none","Invalid unencrypted bootstrap");return {};}
    Require(Text(boot["content_encrypted"])=="true","Invalid encryption flag");const auto method=Text(boot["key_storage"]);auto id=DecodeHex(Text(boot["build_id"]),16),salt=DecodeHex(Text(boot["salt"]),32);std::vector<unsigned char> master;WipeBytes wipe{master};
    if(method=="embedded"){std::ifstream file(executable,std::ios::binary);file.seekg(std::streamoff(PlayerSealOffset(executable)+80));master.resize(32);Bytes(file,master.data(),32);}
    else if(method=="file"){auto data=Read(executable.parent_path()/"game.key",32);Require(data.size()==32,"Invalid key file");master.assign(data.begin(),data.end());SecureZeroMemory(data.data(),data.size());}
    else if(method=="environment"){auto name=Text(boot["key_environment"]);Require(!name.empty()&&name.size()<=101&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==std::string::npos,"Invalid key environment name");auto* value=std::getenv(name.c_str());Require(value&&std::strlen(value)==64,"Game key is unavailable");master=DecodeHex(value,32);}
    else throw std::runtime_error("Unsupported Linux key protection method");
    auto wrapping=Derive(master,salt,"wrapping");WipeBytes wipeWrapping{wrapping};return DecryptEnvelope(wrapping,DecodeHex(Text(boot["wrapped_key"]),60),id);
}
inline std::string ManifestText(const std::string& stored,const std::vector<unsigned char>& root,const std::vector<unsigned char>& salt,const std::vector<unsigned char>& id,const std::string& purpose,bool encrypted){
    if(!encrypted){Require(stored.rfind("BZENC003",0)!=0,"Unexpected encrypted manifest");return stored;}Require(stored.rfind("BZENC003",0)==0,"Missing encrypted manifest");auto key=Derive(root,salt,purpose);WipeBytes wipe{key};auto aad=id;aad.insert(aad.end(),purpose.begin(),purpose.end());auto plain=DecryptEnvelope(key,{stored.begin()+8,stored.end()},aad);WipeBytes wipePlain{plain};return {plain.begin(),plain.end()};
}
}
