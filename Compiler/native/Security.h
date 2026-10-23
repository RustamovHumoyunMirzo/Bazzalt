#pragma once
#include "Package.h"
#include <wincrypt.h>

namespace Bazzalt::Game {
struct ProtectedBlob {
    DATA_BLOB Value{};
    ~ProtectedBlob(){if(Value.pbData){SecureZeroMemory(Value.pbData,Value.cbData);LocalFree(Value.pbData);}}
};
inline std::vector<unsigned char> DecodeHex(const std::string& text,size_t expected=0){
    Require(text.size()%2==0&&text.size()<=32768&&(!expected||text.size()==expected*2),"Invalid security field size");
    const auto digit=[](char c)->unsigned char{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("Invalid security field encoding");};
    std::vector<unsigned char> result;for(size_t i=0;i<text.size();i+=2)result.push_back(static_cast<unsigned char>((digit(text[i])<<4)|digit(text[i+1])));return result;
}
inline std::vector<unsigned char> Hmac256(const std::vector<unsigned char>& key,const std::vector<unsigned char>& data){
    Algorithm alg;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0,"HMAC unavailable");
    std::vector<unsigned char> digest(32);Require(BCryptHash(alg.Value,const_cast<PUCHAR>(key.data()),ULONG(key.size()),const_cast<PUCHAR>(data.data()),ULONG(data.size()),digest.data(),32)>=0,"HMAC failed");return digest;
}
inline std::vector<unsigned char> Derive(const std::vector<unsigned char>& key,const std::vector<unsigned char>& salt,const std::string& purpose){
    // RFC 5869 HKDF-SHA256 with one 32-byte output block. BCrypt supplies
    // HMAC; both stages are checked against the compiler implementation.
    auto prk=Hmac256(salt,key);WipeBytes wipe{prk};const auto info="Bazzalt/v3/"+purpose;
    std::vector<unsigned char> input(info.begin(),info.end());input.push_back(1);return Hmac256(prk,input);
}
inline std::vector<unsigned char> DecryptEnvelope(const std::vector<unsigned char>& key,const std::vector<unsigned char>& envelope,const std::vector<unsigned char>& aad){
    Require(key.size()==32&&envelope.size()>=28&&envelope.size()<=64u*1024u*1024u,"Invalid encrypted envelope");
    Algorithm alg;Key aes;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_AES_ALGORITHM,nullptr,0)>=0,"AES unavailable");
    Require(BCryptSetProperty(alg.Value,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0,"GCM unavailable");
    Require(BCryptGenerateSymmetricKey(alg.Value,&aes.Value,nullptr,0,const_cast<PUCHAR>(key.data()),32,0)>=0,"Could not initialize envelope key");
    const auto size=ULONG(envelope.size()-28);BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=const_cast<PUCHAR>(envelope.data());info.cbNonce=12;info.pbTag=const_cast<PUCHAR>(envelope.data()+envelope.size()-16);info.cbTag=16;info.pbAuthData=const_cast<PUCHAR>(aad.data());info.cbAuthData=ULONG(aad.size());
    std::vector<unsigned char> plain(size?size:1);ULONG count=0;
    if(BCryptDecrypt(aes.Value,const_cast<PUCHAR>(envelope.data()+12),size,&info,nullptr,0,plain.data(),ULONG(plain.size()),&count,0)<0||count!=size){SecureZeroMemory(plain.data(),plain.size());throw std::runtime_error("Encrypted manifest or key authentication failed");}
    plain.resize(size);return plain;
}
inline std::vector<unsigned char> LoadRootKey(ryml::ConstNodeRef boot,const std::filesystem::path& executable){
    if(Text(boot["content_encrypted"])=="false"){Require(Text(boot["manifests_encrypted"])=="false"&&Text(boot["key_storage"])=="none","Invalid unencrypted bootstrap");return {};}
    Require(Text(boot["content_encrypted"])=="true","Invalid content encryption flag");
    const auto method=Text(boot["key_storage"]);const auto id=DecodeHex(Text(boot["build_id"]),16);const auto salt=DecodeHex(Text(boot["salt"]),32);
    std::vector<unsigned char> master;WipeBytes wipe{master};
    if(method=="embedded"){
        std::ifstream file(executable,std::ios::binary);file.seekg(std::streamoff(PlayerSealOffset(executable)+80));master.resize(32);Bytes(file,master.data(),master.size());
    }else if(method=="file"){
        auto raw=Read(executable.parent_path()/"game.key",32);Require(raw.size()==32,"Invalid external key file");master.assign(raw.begin(),raw.end());SecureZeroMemory(raw.data(),raw.size());
    }else if(method=="environment"){
        const auto name=std::filesystem::u8path(Text(boot["key_environment"])).wstring();Require(!name.empty()&&name.size()<=101,"Invalid key environment name");
        std::array<wchar_t,66> buffer{};const auto n=GetEnvironmentVariableW(name.c_str(),buffer.data(),DWORD(buffer.size()));
        if(n!=64){SecureZeroMemory(buffer.data(),sizeof(buffer));throw std::runtime_error("Game key is unavailable; configure the required environment variable");}
        try{master=HexKey(buffer.data());}catch(...){SecureZeroMemory(buffer.data(),sizeof(buffer));throw;}SecureZeroMemory(buffer.data(),sizeof(buffer));
    }else if(method=="dpapi_user"){
        auto blob=DecodeHex(Text(boot["protected_key"]));Require(!blob.empty(),"Missing account-protected key");
        DATA_BLOB input{DWORD(blob.size()),blob.data()},entropy{DWORD(id.size()),const_cast<BYTE*>(id.data())};ProtectedBlob output;
        Require(CryptUnprotectData(&input,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output.Value)!=0,"Game key belongs to another Windows account or is corrupted");
        Require(output.Value.cbData==32,"Invalid account-protected key");master.assign(output.Value.pbData,output.Value.pbData+32);
    }else throw std::runtime_error("Unsupported key protection method");
    auto wrapping=Derive(master,salt,"wrapping");WipeBytes wipeWrapping{wrapping};auto root=DecryptEnvelope(wrapping,DecodeHex(Text(boot["wrapped_key"]),60),id);Require(root.size()==32,"Invalid wrapped build key");return root;
}
inline std::string ManifestText(const std::string& stored,const std::vector<unsigned char>& root,const std::vector<unsigned char>& salt,const std::vector<unsigned char>& id,const std::string& purpose,bool encrypted){
    if(!encrypted){Require(stored.rfind("BZENC003",0)!=0,"Unexpected encrypted manifest");return stored;}
    Require(stored.rfind("BZENC003",0)==0,"Missing encrypted manifest envelope");auto key=Derive(root,salt,purpose);WipeBytes wipeKey{key};
    auto aad=id;aad.insert(aad.end(),purpose.begin(),purpose.end());auto decoded=DecryptEnvelope(key,std::vector<unsigned char>(stored.begin()+8,stored.end()),aad);WipeBytes wipeDecoded{decoded};return std::string(decoded.begin(),decoded.end());
}
}
