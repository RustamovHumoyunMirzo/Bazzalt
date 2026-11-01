#pragma once
#include "Bazzalt/Prefab.h"
#include "Bazzalt/Serialization.h"
#include "Runtime/PreparedScene.h"
namespace Bazzalt::Runtime {
class Engine;
struct PrefabLink:Component {UUID Asset;std::map<UUID,UUID> Entities;std::string Baseline;};
struct PrefabDefinition {UUID Asset;PreparedScene Document;};
struct PrefabAccess {
    static void SetScriptModules(const std::map<std::string,std::filesystem::path>& modules);
    static std::filesystem::path ScriptModule(const std::string& type,const std::filesystem::path& fallback);
    static void Bind(Engine* engine);
    static void Unbind(Engine* engine);
    BAZZALT_API static bool Read(const std::filesystem::path& path,PreparedScene& result,std::string& error);
    BAZZALT_API static bool Save(Entity root,const std::filesystem::path& path,SceneSerializer& serializer,std::string& error,bool overwrite=false);
    BAZZALT_API static bool Link(Entity root,UUID asset,SceneSerializer& serializer,std::string& error);
    static void CookAsset(const std::filesystem::path& source,const std::filesystem::path& destination,const std::map<std::string,UUID>& modules,const std::filesystem::path& project);
    static void CookLinks(Scene& scene,const std::map<std::string,UUID>& modules,const std::filesystem::path& project);
    static void Register(ComponentSerializationRegistry& registry);
};
}
