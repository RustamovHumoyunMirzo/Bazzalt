#pragma once
#include "PosixPackage.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
namespace Bazzalt::Game {
inline std::string DigestHex(const std::array<unsigned char,32>& digest){const char* digits="0123456789abcdef";std::string result;for(auto byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}return result;}
inline std::array<unsigned char,32> HashPrefix(const std::filesystem::path& path,std::uint64_t size){
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(),EVP_MD_CTX_free);Require(hash&&EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)==1,"SHA-256 unavailable");std::ifstream file(path,std::ios::binary);std::vector<unsigned char> buffer(1024*1024);while(size){auto count=std::min<std::uint64_t>(size,buffer.size());Bytes(file,buffer.data(),count);Require(EVP_DigestUpdate(hash.get(),buffer.data(),count)==1,"Hash failed");size-=count;}std::array<unsigned char,32> result{};unsigned count=0;Require(EVP_DigestFinal_ex(hash.get(),result.data(),&count)==1&&count==32,"Hash failed");return result;
}
inline std::array<unsigned char,32> HashFile(const std::filesystem::path& path){return HashPrefix(path,std::filesystem::file_size(path));}
inline std::string PlayerHash(const std::filesystem::path& path){
#ifdef __APPLE__
return MacPlayerHash(path);
#else
return DigestHex(HashPrefix(path,PlayerSealOffset(path)));
#endif
}
struct FileLease {
    int Value=-1;
    explicit FileLease(const std::filesystem::path& path,bool directory=false){Value=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|(directory?O_DIRECTORY:0));Require(Value>=0,"Cannot open verified content");struct stat info{};if(fstat(Value,&info)||bool(S_ISDIR(info.st_mode))!=directory||(!directory&&!S_ISREG(info.st_mode))||flock(Value,LOCK_SH|LOCK_NB)){close(Value);Value=-1;throw std::runtime_error("Linked or invalid verified content");}}
    ~FileLease(){if(Value>=0)close(Value);}FileLease(const FileLease&)=delete;FileLease& operator=(const FileLease&)=delete;
};
using FileLeases=std::vector<std::unique_ptr<FileLease>>;
inline void LockTree(const std::filesystem::path& folder,FileLeases& leases){leases.push_back(std::make_unique<FileLease>(folder,true));std::size_t count=0;for(const auto& entry:std::filesystem::recursive_directory_iterator(folder)){Require(++count<=1000000&&!entry.is_symlink(),"Linked or excessive content tree");if(entry.is_directory())leases.push_back(std::make_unique<FileLease>(entry.path(),true));}}
inline bool VerifySignature(const std::array<unsigned char,64>& coordinates,const std::array<unsigned char,32>& digest,const std::string& signature){
    std::unique_ptr<EC_KEY,decltype(&EC_KEY_free)> key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1),EC_KEY_free);if(!key||signature.size()!=64)return false;
    unsigned char encoded[65]{4};std::copy(coordinates.begin(),coordinates.end(),encoded+1);const auto* group=EC_KEY_get0_group(key.get());std::unique_ptr<EC_POINT,decltype(&EC_POINT_free)> point(EC_POINT_new(group),EC_POINT_free);if(!point||!EC_POINT_oct2point(group,point.get(),encoded,65,nullptr)||!EC_KEY_set_public_key(key.get(),point.get()))return false;
    std::unique_ptr<ECDSA_SIG,decltype(&ECDSA_SIG_free)> value(ECDSA_SIG_new(),ECDSA_SIG_free);auto* r=BN_bin2bn(reinterpret_cast<const unsigned char*>(signature.data()),32,nullptr);auto* s=BN_bin2bn(reinterpret_cast<const unsigned char*>(signature.data()+32),32,nullptr);if(!value||!r||!s){BN_free(r);BN_free(s);return false;}if(!ECDSA_SIG_set0(value.get(),r,s)){BN_free(r);BN_free(s);return false;}return ECDSA_do_verify(digest.data(),digest.size(),value.get(),key.get())==1;
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
    const auto folder=GameDataFolder(executable);VerifiedGame verified;LockTree(folder,verified.OutputLeases);
#ifdef __APPLE__
    // Code is in MacOS, content is in Resources. Authenticate the complete
    // signed bundle before loading any game-owned library, retaining code leases.
    LockTree(executable.parent_path(),verified.OutputLeases);
    for(const auto& entry:std::filesystem::recursive_directory_iterator(executable.parent_path()))
        if(entry.is_regular_file())verified.OutputLeases.push_back(std::make_unique<FileLease>(entry.path()));
    CheckMacSignature(executable);
#endif
    verified.OutputLeases.push_back(std::make_unique<FileLease>(executable));
#ifdef __APPLE__
    verified.OutputLeases.push_back(std::make_unique<FileLease>(SealPath(executable)));
#endif
    for(const auto& name:{"game.bootstrap","game.integrity","game.signature"})verified.OutputLeases.push_back(std::make_unique<FileLease>(folder/name));
    std::ifstream player(SealPath(executable),std::ios::binary);player.seekg(std::streamoff(PlayerSealOffset(executable)));
    std::array<char,8> magic{};Bytes(player,magic.data(),magic.size());Require(std::string(magic.data(),8)=="BZSIG003","Missing version-3 content signature seal; rebuild the game");
    std::array<unsigned char,64> publicBytes{};Bytes(player,publicBytes.data(),publicBytes.size());
    auto bootstrap=Read(folder/"game.bootstrap",65536);auto payload=Read(folder/"game.integrity",64*1024*1024);auto signature=Read(folder/"game.signature",64);Require(signature.size()==64,"Invalid content signature size");
    std::vector<unsigned char> signedBytes{'B','Z','S','I','G','0','0','3'};const auto length=std::uint32_t(bootstrap.size());for(int i=0;i<4;++i)signedBytes.push_back(static_cast<unsigned char>(length>>(i*8)));
    signedBytes.insert(signedBytes.end(),bootstrap.begin(),bootstrap.end());signedBytes.insert(signedBytes.end(),payload.begin(),payload.end());auto digest=Hash(signedBytes);
    Require(VerifySignature(publicBytes,digest,signature),"Game inventory signature is invalid");
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
        Require(!entry.is_symlink(),"Linked game content is forbidden");
        if(!entry.is_regular_file())continue;const auto relative=entry.path().lexically_relative(folder);
        Require(entry.path()==executable||relative=="game.bootstrap"||relative=="game.integrity"||relative=="game.signature"||
#ifdef __APPLE__
relative=="game.seal"||
#endif
relative==".bazzalt-game-build"||listed.count(relative)!=0,"Unexpected file in signed game output");
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
