#pragma once
#include "Security.h"
#include <set>
#include <memory>
#include <algorithm>

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
inline std::string PlayerHash(const std::filesystem::path& executable){
    const auto end=PlayerSealOffset(executable);std::ifstream file(executable,std::ios::binary);IMAGE_DOS_HEADER dos{};file.read(reinterpret_cast<char*>(&dos),sizeof(dos));
    const auto optional=std::uint64_t(dos.e_lfanew)+24;file.seekg(std::streamoff(optional));WORD magic=0;file.read(reinterpret_cast<char*>(&magic),2);Require(bool(file),"Invalid player hash header");
    const auto directory=optional+(magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC?96:112)+32;
    Algorithm alg;HashHandle hash;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(alg.Value,&hash.Value,nullptr,0,nullptr,0,0)>=0,"Player hash unavailable");
    std::vector<unsigned char> buffer(1024*1024);std::uint64_t offset=0;file.clear();file.seekg(0);
    while(offset<end){const auto size=size_t(std::min<std::uint64_t>(buffer.size(),end-offset));Bytes(file,buffer.data(),size);
        for(const auto range:{std::pair<std::uint64_t,std::uint64_t>{optional+64,optional+68},{directory,directory+8}}){
            const auto begin=std::max(offset,range.first),last=std::min(offset+size,range.second);if(begin<last)std::fill(buffer.begin()+size_t(begin-offset),buffer.begin()+size_t(last-offset),0);
        }
        Require(BCryptHashData(hash.Value,buffer.data(),ULONG(size),0)>=0,"Player hash failed");offset+=size;
    }
    std::array<unsigned char,32> result{};Require(BCryptFinishHash(hash.Value,result.data(),32,0)>=0,"Player hash failed");return DigestHex(result);
}
struct FileLease {
    HANDLE Value=INVALID_HANDLE_VALUE;
    explicit FileLease(const std::filesystem::path& path,bool directory=false){
        Value=CreateFileW(path.c_str(),GENERIC_READ,directory?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL),nullptr);
        Require(Value!=INVALID_HANDLE_VALUE,"Could not lock verified content against changes");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(Value,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory){CloseHandle(Value);Value=INVALID_HANDLE_VALUE;throw std::runtime_error("Linked or invalid signed content");}
    }
    ~FileLease(){if(Value!=INVALID_HANDLE_VALUE)CloseHandle(Value);}
    FileLease(const FileLease&)=delete;FileLease& operator=(const FileLease&)=delete;
};
using FileLeases=std::vector<std::unique_ptr<FileLease>>;
inline void LockTree(const std::filesystem::path& folder,FileLeases& leases){
    leases.push_back(std::make_unique<FileLease>(folder,true));size_t entries=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(folder)){
        Require(++entries<=1000000,"Content tree exceeds verification limits");
        const auto flags=GetFileAttributesW(entry.path().c_str());Require(flags!=INVALID_FILE_ATTRIBUTES&&!(flags&FILE_ATTRIBUTE_REPARSE_POINT),"Linked content is forbidden");
        if(entry.is_directory())leases.push_back(std::make_unique<FileLease>(entry.path(),true));
    }
}
struct VerifiedGame {
    FileLeases OutputLeases,ExtractedLeases;
    std::string Configuration,ContentIndex;
    std::vector<unsigned char> PackageKey;
    VerifiedGame()=default;VerifiedGame(VerifiedGame&&)=default;VerifiedGame& operator=(VerifiedGame&&)=default;
    VerifiedGame(const VerifiedGame&)=delete;VerifiedGame& operator=(const VerifiedGame&)=delete;
    ~VerifiedGame(){if(!PackageKey.empty())SecureZeroMemory(PackageKey.data(),PackageKey.size());if(!Configuration.empty())SecureZeroMemory(Configuration.data(),Configuration.size());if(!ContentIndex.empty())SecureZeroMemory(ContentIndex.data(),ContentIndex.size());}
};
inline VerifiedGame VerifyInventory(const std::filesystem::path& executable){
    VerifiedGame verified;LockTree(executable.parent_path(),verified.OutputLeases);
    verified.OutputLeases.push_back(std::make_unique<FileLease>(executable));
    for(const auto& name:{"game.bootstrap","game.integrity","game.signature"})verified.OutputLeases.push_back(std::make_unique<FileLease>(executable.parent_path()/name));
    const auto folder=executable.parent_path();std::ifstream player(executable,std::ios::binary);player.seekg(std::streamoff(PlayerSealOffset(executable)));
    std::array<char,8> magic{};Bytes(player,magic.data(),magic.size());Require(std::string(magic.data(),8)=="BZSIG003","Missing version-3 content signature seal; rebuild the game");
    std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB)+64);BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC,32};std::memcpy(blob.data(),&header,sizeof(header));Bytes(player,blob.data()+sizeof(header),64);
    Algorithm alg;Key publicKey;Require(BCryptOpenAlgorithmProvider(&alg.Value,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0,"Signature verifier unavailable");
    Require(BCryptImportKeyPair(alg.Value,nullptr,BCRYPT_ECCPUBLIC_BLOB,&publicKey.Value,blob.data(),ULONG(blob.size()),0)>=0,"Invalid content verification key");
    auto bootstrap=Read(folder/"game.bootstrap",65536);auto payload=Read(folder/"game.integrity",64*1024*1024);auto signature=Read(folder/"game.signature",64);Require(signature.size()==64,"Invalid content signature size");
    std::vector<unsigned char> signedBytes{'B','Z','S','I','G','0','0','3'};const auto length=std::uint32_t(bootstrap.size());for(int i=0;i<4;++i)signedBytes.push_back(static_cast<unsigned char>(length>>(i*8)));
    signedBytes.insert(signedBytes.end(),bootstrap.begin(),bootstrap.end());signedBytes.insert(signedBytes.end(),payload.begin(),payload.end());auto digest=Hash(signedBytes);
    Require(BCryptVerifySignature(publicKey.Value,nullptr,digest.data(),ULONG(digest.size()),reinterpret_cast<PUCHAR>(signature.data()),ULONG(signature.size()),0)>=0,"Game inventory signature is invalid");
    auto bootTree=ryml::parse_in_arena(ryml::csubstr(bootstrap.data(),bootstrap.size()));auto boot=bootTree.rootref();Require(Text(boot["format_version"])=="3","Unsupported security bootstrap");
    Require(PlayerHash(executable)==Text(boot["player_hash"]),"Player executable hash mismatch");
    player.clear();player.seekg(std::streamoff(PlayerSealOffset(executable)));std::vector<unsigned char> footer(112);WipeBytes wipeFooter{footer};Bytes(player,footer.data(),footer.size());Require(DigestHex(Hash(footer))==Text(boot["seal_hash"]),"Player key seal hash mismatch");
    if(Text(boot["key_storage"])=="file")verified.OutputLeases.push_back(std::make_unique<FileLease>(folder/"game.key"));
    auto rootKey=LoadRootKey(boot,executable);WipeBytes wipeRoot{rootKey};auto salt=DecodeHex(Text(boot["salt"]),32),id=DecodeHex(Text(boot["build_id"]),16);
    const auto flag=Text(boot["manifests_encrypted"]);Require(flag=="true"||flag=="false","Invalid manifest encryption flag");const bool encrypted=flag=="true";
    payload=ManifestText(payload,rootKey,salt,id,"inventory",encrypted);
    auto tree=ryml::parse_in_arena(ryml::csubstr(payload.data(),payload.size()));auto root=tree.rootref();Require(Text(root["format_version"])=="3"&&Text(root["build_id"])==Text(boot["build_id"]),"Unsupported or mismatched signed inventory");
    std::set<std::filesystem::path> listed;size_t count=0;
    for(auto record:root["files"].children()){
        Require(++count<=1000000,"Signed inventory is too large");auto relative=SafePath(Text(record["path"]));const auto path=folder/relative;
        verified.OutputLeases.push_back(std::make_unique<FileLease>(path));
        Require(listed.insert(relative).second&&std::filesystem::is_regular_file(path),"Missing or duplicate signed file");
        auto size=Text(record["size"]);Require(!size.empty()&&size.find_first_not_of("0123456789")==std::string::npos,"Invalid signed file size");
        Require(std::filesystem::file_size(path)==std::stoull(size)&&DigestHex(HashFile(path))==Text(record["sha256"]),"Game content was modified or corrupted");
    }
    Require(listed.count("game.json")!=0,"Game configuration is not signed");
    for(const auto& entry:std::filesystem::recursive_directory_iterator(folder)){
        Require((GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)==0,"Linked game content is forbidden");
        if(!entry.is_regular_file())continue;const auto relative=entry.path().lexically_relative(folder);
        Require(entry.path()==executable||relative=="game.bootstrap"||relative=="game.integrity"||relative=="game.signature"||relative==".bazzalt-game-build"||listed.count(relative)!=0,"Unexpected file in signed game output");
    }
    verified.Configuration=ManifestText(Read(folder/"game.json"),rootKey,salt,id,"configuration",encrypted);
    verified.ContentIndex=std::move(payload);if(!rootKey.empty())verified.PackageKey=Derive(rootKey,salt,"packages");return verified;
}
inline void VerifyExtracted(VerifiedGame& verified,const std::filesystem::path& folder){
    LockTree(folder,verified.ExtractedLeases);
    auto tree=ryml::parse_in_arena(ryml::csubstr(verified.ContentIndex.data(),verified.ContentIndex.size()));auto content=tree.rootref()["content"];
    std::set<std::filesystem::path> paths;size_t count=0;
    for(auto entry:content.children()){
        Require(++count<=1000000,"Decoded content index exceeds limits");auto key=entry.key();auto relative=SafePath(std::string(key.str,key.len));const auto path=folder/relative;
        Require(paths.insert(relative).second,"Duplicate decoded content path");verified.ExtractedLeases.push_back(std::make_unique<FileLease>(path));
        const auto size=Text(entry["size"]);Require(!size.empty()&&size.find_first_not_of("0123456789")==std::string::npos,"Invalid decoded file size");
        Require(std::filesystem::file_size(path)==std::stoull(size)&&DigestHex(HashFile(path))==Text(entry["sha256"]),"Decoded game content hash mismatch");
    }
    Require(count>0,"Missing decoded content hashes");
    for(const auto& entry:std::filesystem::recursive_directory_iterator(folder))if(entry.is_regular_file()){
        const auto relative=entry.path().lexically_relative(folder);Require(relative==".bazzalt-session"||paths.count(relative)!=0,"Unexpected decoded content file");
    }
}
}
