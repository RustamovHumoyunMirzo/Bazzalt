#include "Runtime/MaterialLibrary.h"
#include "Bazzalt/AssetManager.h"
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace Bazzalt::Runtime {
namespace {
struct Definition {
    UUID Shader{};
    std::string Stamp;
    std::filesystem::file_time_type Modified{};
    std::string ShaderStamp;
    std::vector<ShaderParameter> Parameters;
    std::unordered_map<std::string,Detail::MaterialValue> Values;
    std::unordered_map<std::string,Detail::MaterialValue> Overrides;
    bool Transient=false;
};
std::unordered_map<UUID,Definition> Shaders,Materials;
std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};
}
std::string Text(ryml::ConstNodeRef node) { auto v=node.val();return {v.str,v.len}; }
std::string Extension(const std::filesystem::path& path){auto text=path.extension().string();std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return text;}
bool ParseValue(ryml::ConstNodeRef node,ShaderParameterType type,Detail::MaterialValue& value) {
    try {
        if(type==ShaderParameterType::Texture2D)return UUID::TryParse(Text(node),value.Texture);
        if(type==ShaderParameterType::Boolean){const auto text=Text(node);if(text!="true"&&text!="false")return false;value.Boolean=text=="true";return true;}
        if(type==ShaderParameterType::Integer){const auto text=Text(node);std::size_t end=0;auto i=std::stoll(text,&end);if(end!=text.size()||i<INT32_MIN||i>INT32_MAX)return false;value.Integer=static_cast<std::int32_t>(i);return true;}
        const auto count=type==ShaderParameterType::Float2?2:type==ShaderParameterType::Float3?3:type==ShaderParameterType::Float4?4:type==ShaderParameterType::Matrix3?9:type==ShaderParameterType::Matrix4?16:1;
        const auto parse=[](const std::string& text,float& out){std::size_t end=0;out=std::stof(text,&end);return end==text.size()&&std::isfinite(out);};
        if(count==1)return parse(Text(node),value.Numbers[0]);
        if(!node.is_seq()||node.num_children()!=count)return false;
        std::size_t i=0;for(auto child:node.children())if(!parse(Text(child),value.Numbers[i++]))return false;
        return true;
    } catch(...) { return false; }
}
Definition* ShaderDefinition(UUID id) {
    const auto asset=AssetManager::GetAsset(id);if(!asset)return nullptr;
    const auto ext=Extension(asset->SourcePath);if(ext!=".mat"&&ext!=".shad")return nullptr;
    auto path=asset->CachePath;path+=".reflection.json";std::error_code error;const auto modified=std::filesystem::last_write_time(path,error);if(error)return nullptr;
    auto& result=Shaders[id];if(!result.Stamp.empty()&&result.Modified==modified)return &result;
    const auto json=Read(path);if(json.empty())return nullptr;
    try {
        auto tree=ryml::parse_in_arena(ryml::to_csubstr(json));auto root=tree.crootref();
        if(!root.has_child("parameters")||!root["parameters"].is_seq())return nullptr;
        Definition next;next.Stamp=json;next.Modified=modified;next.Shader=id;
        for(auto p:root["parameters"].children()) {
            if(!p.has_child("name")||!p.has_child("kind"))return nullptr;
            auto name=Text(p["name"]);auto kind=std::stoi(Text(p["kind"]));if(kind<0||kind>8||name.size()>255)return nullptr;
            ShaderParameter parameter{name,static_cast<ShaderParameterType>(kind)};next.Parameters.push_back(parameter);
            Detail::MaterialValue value;if(p.has_child("default")&&!ParseValue(p["default"],parameter.Type,value))return nullptr;
            next.Values[name]=value;
        }
        result=std::move(next);return &result;
    }catch(...){return nullptr;}
}
Definition* MaterialDefinition(UUID id) {
    if(auto found=Materials.find(id);found!=Materials.end()&&found->second.Transient)return &found->second;
    const auto asset=AssetManager::GetAsset(id);if(!asset||Extension(asset->SourcePath)!=".matinst")return nullptr;
    std::error_code error;const auto modified=std::filesystem::last_write_time(asset->SourcePath,error);if(error)return nullptr;
    if(auto found=Materials.find(id);found!=Materials.end()&&found->second.Modified==modified){if(!found->second.Shader)return &found->second;auto* shader=ShaderDefinition(found->second.Shader);if(shader&&found->second.ShaderStamp==shader->Stamp)return &found->second;}
    const auto json=Read(asset->SourcePath);if(json.empty())return nullptr;
    try {
        auto tree=ryml::parse_in_arena(ryml::to_csubstr(json));auto root=tree.crootref();UUID shader;
        if(root.has_child("version")&&Text(root["version"])!="1")return nullptr;
        if(!root.has_child("shader"))return nullptr;
        const auto reference=Text(root["shader"]);
        if(reference!="null"&&!reference.empty()&&!UUID::TryParse(reference,shader))return nullptr;
        if(!shader){auto& result=Materials[id];result=Definition{};result.Stamp=json;result.Modified=modified;return &result;}
        auto* definition=ShaderDefinition(shader);if(!definition)return nullptr;
        const auto stamp=json+definition->Stamp;auto& result=Materials[id];if(result.Stamp==stamp)return &result;
        Definition next=*definition;next.Shader=shader;next.Stamp=stamp;next.ShaderStamp=definition->Stamp;next.Modified=modified;next.Overrides=result.Overrides;
        if(root.has_child("properties"))for(const auto& p:next.Parameters)if(root["properties"].has_child(ryml::to_csubstr(p.Name))){
            if(!ParseValue(root["properties"][ryml::to_csubstr(p.Name)],p.Type,next.Values[p.Name]))return nullptr;
        }
        result=std::move(next);return &result;
    }catch(...){return nullptr;}
}
bool Valid(UUID id,bool shader){return shader?ShaderDefinition(id)!=nullptr:MaterialDefinition(id)!=nullptr;}
UUID GetShader(UUID id){auto* d=MaterialDefinition(id);return d?d->Shader:UUID{};}
bool Parameter(UUID id,std::uint32_t index,char* name,std::uint32_t capacity,ShaderParameterType* type){
    auto* d=ShaderDefinition(id);if(!d||index>=d->Parameters.size())return false;
    const auto& p=d->Parameters[index];if(capacity<=p.Name.size())return false;std::memcpy(name,p.Name.c_str(),p.Name.size()+1);*type=p.Type;return true;
}
bool Get(UUID id,const char* name,Detail::MaterialValue* value){auto* d=MaterialDefinition(id);if(!d||!name||!value)return false;
    if(auto f=d->Overrides.find(name);f!=d->Overrides.end()){*value=f->second;return true;}
    auto f=d->Values.find(name);if(f==d->Values.end())return false;*value=f->second;return true;}
bool Set(UUID id,const char* name,ShaderParameterType type,const Detail::MaterialValue* value){auto* d=MaterialDefinition(id);if(!d||!value||!name)return false;
    for(const auto& p:d->Parameters)if(p.Name==name&&p.Type==type){for(float v:value->Numbers)if(!std::isfinite(v))return false;
        if(type==ShaderParameterType::Texture2D&&value->Texture){auto a=AssetManager::GetAsset(value->Texture);if(!a)return false;const auto ext=Extension(a->SourcePath);if(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg")return false;}
        d->Overrides[name]=*value;return true;}return false;}
UUID Clone(UUID id,bool shader){auto* d=shader?ShaderDefinition(id):MaterialDefinition(id);if(!d)return {};auto copy=*d;copy.Transient=true;copy.Shader=shader?id:d->Shader;auto uuid=UUID::Generate();Materials.emplace(uuid,std::move(copy));return uuid;}
Detail::MaterialServices Services{Valid,GetShader,Parameter,Get,Set,Clone};
}
Detail::MaterialServices* GetMaterialServices(){return &Services;}
void ResetMaterialLibrary(){Materials.clear();Shaders.clear();}
void ResetRuntimeMaterials(){for(auto it=Materials.begin();it!=Materials.end();)if(it->second.Transient)it=Materials.erase(it);else{it->second.Overrides.clear();++it;}}
}
