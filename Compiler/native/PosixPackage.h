#pragma once
#include "PosixCrypto.h"
extern "C" size_t ZSTD_decompress(void*,size_t,const void*,size_t);
extern "C" unsigned ZSTD_isError(size_t);
namespace Bazzalt::Game {
inline void Extract(const std::filesystem::path& folder,const std::filesystem::path& output,const std::vector<std::string>& packages,const std::vector<unsigned char>& key){
    struct Progress{std::uint64_t Offset=0,Total=0;};std::map<std::string,Progress> progress;std::uint64_t allocated=0;size_t records=0;

    std::vector<unsigned char> digestKey;WipeBytes wipeDigestKey{digestKey};

    if(!key.empty()){
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
                Require(names>28,"Invalid encrypted package path");auto decoded=DecryptGcm(key,reinterpret_cast<const unsigned char*>(encodedName.data()+12),names-28,reinterpret_cast<const unsigned char*>(encodedName.data()),reinterpret_cast<const unsigned char*>(encodedName.data()+names-16),header);name.assign(decoded.begin(),decoded.end());SecureZeroMemory(decoded.data(),decoded.size());
            }
            auto path=SafePath(name);
            // Case-insensitive filesystem aliases must not overwrite a previous entry.
            auto canonical=path.generic_string();for(auto& c:canonical)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            auto [found,inserted]=progress.try_emplace(canonical);auto& item=found->second;
            if(inserted){allocated+=total;Require(allocated<=32ull*1024*1024*1024,"Package extraction limit exceeded");item.Total=total;Require(!std::filesystem::exists(output/path),"Duplicate package path");}
            else Require(item.Offset<item.Total,"Duplicate package file");
            Require(item.Offset==offset&&item.Total==total,"Out-of-order package segment");
            std::array<unsigned char,12> nonce{};std::array<unsigned char,16> tag{};std::array<unsigned char,32> digest{};Bytes(f,nonce.data(),12);Bytes(f,tag.data(),16);Bytes(f,digest.data(),32);std::vector<unsigned char> data(size);Bytes(f,data.data(),size);
            if(encrypted){
                auto aad=std::vector<unsigned char>(header.begin(),header.end());aad.insert(aad.end(),encodedName.begin(),encodedName.end());
                data=DecryptGcm(key,data.data(),data.size(),nonce.data(),tag.data(),aad);
            }
            Require((version3&&encrypted?Hash(data,digestKey):Hash(data))==digest,"Package checksum mismatch");std::filesystem::create_directories((output/path).parent_path());
            if(alias){
                auto target=SafePath(std::string(data.begin(),data.end()));auto canonical=target.generic_string();for(auto& c:canonical)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
                const auto previous=progress.find(canonical);Require(previous!=progress.end()&&previous!=found&&previous->second.Offset==total&&previous->second.Total==total,"Invalid package alias");
                Require(std::filesystem::is_regular_file(output/target)&&std::filesystem::file_size(output/target)==total,"Missing package alias target");
                std::filesystem::copy_file(output/target,output/path);item.Offset=total;continue;
            }
            if(compressed){Require((flags&8)!=0,"Legacy Windows-only package compression");std::vector<unsigned char> plain(rawSize);auto count=ZSTD_decompress(plain.data(),plain.size(),data.data(),data.size());Require(!ZSTD_isError(count)&&count==rawSize,"Invalid compressed payload");data=std::move(plain);}
            std::ofstream file(output/path,std::ios::binary|(offset?std::ios::app:std::ios::trunc));file.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));Require(bool(file),"Could not extract game content");item.Offset+=rawSize;
        }
    }
    for(const auto& [_,item]:progress)Require(item.Offset==item.Total,"Incomplete game package");
}
}
