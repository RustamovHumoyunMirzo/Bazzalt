#include "Runtime/TextureLibrary.h"
#include "Bazzalt/AssetManager.h"
#include <ryml.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <stdexcept>
#include <iomanip>
#include <limits>
#include <type_traits>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
namespace Bazzalt {
std::uint8_t TextureDescriptor::GetMipLevelCount() const{
    if(!Mipmaps)return 1;unsigned levels=1,size=std::max(Width,Height);while(size>1){++levels;size>>=1;}
    return static_cast<std::uint8_t>(MipLevels?MipLevels:levels);
}
bool TextureDescriptor::IsValid() const{
    unsigned levels=1,size=std::max(Width,Height);while(size>1){++levels;size>>=1;}
    return Width&&Height&&Width<=4096&&Height<=4096&&(Samples==1||Samples==2||Samples==4||Samples==8)&&
        static_cast<unsigned>(ColorFormat)<=2&&static_cast<unsigned>(DepthFormat)<=3&&static_cast<unsigned>(Filter)<=2&&static_cast<unsigned>(Wrap)<=2&&
        MipLevels<=levels&&(!Mipmaps?MipLevels<=1:true)&&std::isfinite(ClearColor.X)&&std::isfinite(ClearColor.Y)&&std::isfinite(ClearColor.Z)&&std::isfinite(ClearColor.W)&&
        ClearColor.W>=0&&ClearColor.W<=1;
}
namespace Runtime {
namespace {
struct Entry {TextureDescriptor Value;std::optional<TextureDescriptor> Override;bool Transient=false;std::filesystem::file_time_type Modified{};};
std::unordered_map<UUID,Entry> Textures;
constexpr const char* ColorNames[]={"RGBA8","RGBA16F","RGBA32F"};
constexpr const char* DepthNames[]={"None","Depth16","Depth24","Depth32F"};
constexpr const char* FilterNames[]={"Point","Linear","Trilinear"};
constexpr const char* WrapNames[]={"Clamp","Repeat","Mirror"};
std::string Text(ryml::ConstNodeRef node){auto value=node.val();return {value.str,value.len};}
Entry* Find(UUID id){
    if(auto found=Textures.find(id);found!=Textures.end()&&found->second.Transient)return &found->second;
    auto asset=AssetManager::GetAsset(id);if(!asset||asset->Importer!="Bazzalt.RenderTexture"||asset->State!=AssetState::Ready)return nullptr;
    std::error_code ec;const auto modified=std::filesystem::last_write_time(asset->SourcePath,ec);if(ec)return nullptr;
    auto& entry=Textures[id];if(entry.Modified!=modified){TextureDescriptor descriptor;std::string error;if(!ReadTextureDescriptor(asset->SourcePath,descriptor,error))return nullptr;entry.Value=descriptor;entry.Modified=modified;}
    return &entry;
}
}
bool ReadTextureDescriptor(const std::filesystem::path& path,TextureDescriptor& value,std::string& error){
    try{
        std::error_code sizeError;const auto size=std::filesystem::file_size(path,sizeError);if(sizeError||size>65536)throw std::runtime_error("Cannot read texture descriptor (maximum 64 KiB)");
        std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read texture asset");std::string data(65537,'\0');file.read(data.data(),data.size());data.resize(static_cast<std::size_t>(file.gcount()));if(data.size()>65536)throw std::runtime_error("Texture descriptor exceeds 64 KiB");
        auto tree=ryml::parse_in_arena(ryml::to_csubstr(data));auto root=tree.crootref();if(!root.is_map()||!root.has_child("FormatVersion")||Text(root["FormatVersion"])!="1")throw std::runtime_error("Unsupported texture descriptor version");
        TextureDescriptor next;
        const auto number=[&](const char* key,auto& target){if(!root.has_child(key))return;const auto text=Text(root[key]);unsigned parsed=0;auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||parsed>std::numeric_limits<std::remove_reference_t<decltype(target)>>::max())throw std::runtime_error("Invalid texture number");target=static_cast<std::remove_reference_t<decltype(target)>>(parsed);};
        const auto enumeration=[&](const char* key,auto& target,const auto& names){if(!root.has_child(key))return;auto text=Text(root[key]);for(unsigned i=0;i<std::size(names);++i)if(text==names[i]){target=static_cast<std::remove_reference_t<decltype(target)>>(i);return;}throw std::runtime_error("Invalid texture enum");};
        number("Width",next.Width);number("Height",next.Height);number("Samples",next.Samples);number("MipLevels",next.MipLevels);
        if(root.has_child("Mipmaps")){auto text=Text(root["Mipmaps"]);if(text!="true"&&text!="false")throw std::runtime_error("Invalid Mipmaps flag");next.Mipmaps=text=="true";}
        enumeration("ColorFormat",next.ColorFormat,ColorNames);enumeration("DepthFormat",next.DepthFormat,DepthNames);enumeration("Filter",next.Filter,FilterNames);enumeration("Wrap",next.Wrap,WrapNames);
        if(root.has_child("ClearColor")){auto color=root["ClearColor"];if(!color.is_seq()||color.num_children()!=4)throw std::runtime_error("ClearColor must contain four numbers");float* output[]={&next.ClearColor.X,&next.ClearColor.Y,&next.ClearColor.Z,&next.ClearColor.W};unsigned i=0;for(auto part:color.children()){auto text=Text(part);std::size_t end=0;*output[i++]=std::stof(text,&end);if(end!=text.size())throw std::runtime_error("Invalid clear color");}}
        if(!next.IsValid())throw std::runtime_error("Invalid texture settings");value=next;error.clear();return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool SaveTextureDescriptor(const std::filesystem::path& path,const TextureDescriptor& value,std::string& error){
    if(!value.IsValid()){error="Invalid texture settings";return false;}
    // Editor-owned writer; public Texture setters never call this function.
    auto temporary=path;temporary+=".tmp-"+UUID::Generate().ToString();
    {std::ofstream file(temporary,std::ios::binary);file<<std::setprecision(9)<<"FormatVersion: 1\nWidth: "<<value.Width<<"\nHeight: "<<value.Height<<"\nSamples: "<<unsigned(value.Samples)<<"\nMipmaps: "<<(value.Mipmaps?"true":"false")<<"\nMipLevels: "<<unsigned(value.MipLevels)<<"\nColorFormat: "<<ColorNames[unsigned(value.ColorFormat)]<<"\nDepthFormat: "<<DepthNames[unsigned(value.DepthFormat)]<<"\nFilter: "<<FilterNames[unsigned(value.Filter)]<<"\nWrap: "<<WrapNames[unsigned(value.Wrap)]<<"\nClearColor: ["<<value.ClearColor.X<<", "<<value.ClearColor.Y<<", "<<value.ClearColor.Z<<", "<<value.ClearColor.W<<"]\n";file.flush();const bool saved=bool(file);file.close();if(!saved){error="Cannot write texture asset";std::error_code ec;std::filesystem::remove(temporary,ec);return false;}}
    std::error_code ec;
#ifdef _WIN32
    // std::filesystem::rename cannot replace existing files on Windows.
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error="Cannot replace texture asset";std::filesystem::remove(temporary,ec);return false;}
#else
    std::filesystem::rename(temporary,path,ec);if(ec){error=ec.message();std::filesystem::remove(temporary,ec);return false;}
#endif
    // Atomic replacements can share a filesystem timestamp on rapid saves.
    // Explicitly invalidate cached source settings while preserving overrides.
    if(auto asset=AssetManager::GetAsset(path))if(auto entry=Textures.find(asset->Id);entry!=Textures.end())entry->second.Modified=std::filesystem::file_time_type::min();
    error.clear();return true;
}
std::optional<TextureDescriptor> GetTextureDescriptor(UUID id){auto* entry=Find(id);return entry?std::optional(entry->Override.value_or(entry->Value)):std::nullopt;}
void ResetTextureLibrary(){Textures.clear();}
void ResetRuntimeTextures(){std::erase_if(Textures,[](const auto& entry){return entry.second.Transient;});for(auto& [id,entry]:Textures)entry.Override.reset();}
}
Texture Texture::Create(const TextureDescriptor& descriptor){if(!descriptor.IsValid())throw std::invalid_argument("Invalid texture descriptor");auto id=UUID::Generate();Runtime::Textures[id]={descriptor,{},true,{}};return Load(id);}
bool Texture::IsValid() const{if(IsRenderTarget())return true;auto asset=AssetManager::GetAsset(m_id);return asset&&asset->State==AssetState::Ready&&asset->Importer=="Bazzalt.Texture";}
bool Texture::IsRenderTarget() const{return Runtime::GetTextureDescriptor(m_id).has_value();}
bool Texture::IsRuntime() const{auto* entry=Runtime::Find(m_id);return entry&&entry->Transient;}
TextureDescriptor Texture::GetDescriptor() const{auto value=Runtime::GetTextureDescriptor(m_id);if(!value)throw std::invalid_argument("Invalid renderable texture");return *value;}
void Texture::SetDescriptor(const TextureDescriptor& descriptor){auto* entry=Runtime::Find(m_id);if(!entry||!descriptor.IsValid())throw std::invalid_argument("Invalid texture descriptor or handle");entry->Override=descriptor;}
void Texture::ResetDescriptor(){auto* entry=Runtime::Find(m_id);if(!entry)throw std::invalid_argument("Invalid texture");entry->Override.reset();}
void Texture::Resize(std::uint32_t width,std::uint32_t height){auto descriptor=GetDescriptor();descriptor.Width=width;descriptor.Height=height;unsigned levels=1,size=std::max(width,height);while(size>1){++levels;size>>=1;}descriptor.MipLevels=static_cast<std::uint8_t>(std::min<unsigned>(descriptor.MipLevels,levels));SetDescriptor(descriptor);}
bool Texture::Destroy(){auto found=Runtime::Textures.find(m_id);if(found==Runtime::Textures.end()||!found->second.Transient)return false;Runtime::Textures.erase(found);return true;}
}
