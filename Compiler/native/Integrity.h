#pragma once
#include "Package.h"
#include <set>

namespace Bazzalt::Game {
struct HashHandle {BCRYPT_HASH_HANDLE Value{};~HashHandle(){if(Value)BCryptDestroyHash(Value);}};
inline std::array<unsigned char,32> HashFile(const std::filesystem::path& path){
    Algorithm alg;HashHandle hash;
    Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA-256 unavailable");
    Require(BCryptCreateHash(alg.Value,&hash.Value,nullptr,0,nullptr,0,0)>=0,"Could not initialize file hash");
    std::ifstream file(path,std::ios::binary);Require(bool(file),"Could not open signed file");std::vector<unsigned char> buffer(1024*1024);
    while(file){file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=file.gcount();if(n)Require(BCryptHashData(hash.Value,buffer.data(),ULONG(n),0)>=0,"Could not hash signed file");}
    Require(file.eof(),"Could not read signed file");std::array<unsigned char,32> digest{};Require(BCryptFinishHash(hash.Value,digest.data(),ULONG(digest.size()),0)>=0,"Could not finish file hash");return digest;
}
inline std::string DigestHex(const std::array<unsigned char,32>& digest){
    const char* digits="0123456789abcdef";std::string result;for(auto byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
inline void VerifyInventory(const std::filesystem::path& executable){
    const auto folder=executable.parent_path();std::ifstream player(executable,std::ios::binary);player.seekg(std::streamoff(PlayerSealOffset(executable)));
    std::array<char,8> magic{};Bytes(player,magic.data(),magic.size());Require(std::string(magic.data(),8)=="BZSIG002","Missing game content signature seal; rebuild the game");
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB)+64);BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC,32};std::memcpy(blob.data(),&header,sizeof(header));Bytes(player,blob.data()+sizeof(header),64);
    Algorithm alg;Key publicKey;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0,"Signature verifier unavailable");
    Require(BCryptImportKeyPair(alg.Value,nullptr,BCRYPT_ECCPUBLIC_BLOB,&publicKey.Value,blob.data(),ULONG(blob.size()),0)>=0,"Invalid content verification key");
    auto payload=Read(folder/"game.integrity",64*1024*1024);auto signature=Read(folder/"game.signature",64);Require(signature.size()==64,"Invalid content signature size");
    auto digest=Hash(std::vector<unsigned char>(payload.begin(),payload.end()));
    Require(BCryptVerifySignature(publicKey.Value,nullptr,digest.data(),ULONG(digest.size()),reinterpret_cast<PUCHAR>(signature.data()),ULONG(signature.size()),0)>=0,"Game inventory signature is invalid");
    auto tree=ryml::parse_in_arena(ryml::csubstr(payload.data(),payload.size()));auto root=tree.rootref();Require(Text(root["format_version"])=="1","Unsupported signed inventory");
    std::set<std::filesystem::path> listed;size_t count=0;
    for(auto record:root["files"].children()){
        Require(++count<=1000000,"Signed inventory is too large");auto relative=SafePath(Text(record["path"]));const auto path=folder/relative;
        Require(listed.insert(relative).second&&std::filesystem::is_regular_file(path),"Missing or duplicate signed file");
        auto size=Text(record["size"]);Require(!size.empty()&&size.find_first_not_of("0123456789")==std::string::npos,"Invalid signed file size");
        Require(std::filesystem::file_size(path)==std::stoull(size)&&DigestHex(HashFile(path))==Text(record["sha256"]),"Game content was modified or corrupted");
    }
    Require(listed.count("game.json")!=0,"Game configuration is not signed");
    for(const auto& entry:std::filesystem::recursive_directory_iterator(folder)){
        Require((GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)==0,"Linked game content is forbidden");
        if(!entry.is_regular_file())continue;const auto relative=entry.path().lexically_relative(folder);
        Require(entry.path()==executable||relative=="game.integrity"||relative=="game.signature"||relative==".bazzalt-game-build"||listed.count(relative)!=0,"Unexpected file in signed game output");
    }
}
}
