#include "Script/LuaBindings.h"
#include "Bazzalt/Prefab.h"
namespace Bazzalt::Runtime {
void BindLuaPrefabs(sol::table api){
 auto type=api.new_usertype<Prefab>("Prefab",sol::no_constructor);
 type["Load"]=&Prefab::Load;type["GetUUID"]=&Prefab::GetUUID;type["GetName"]=&Prefab::GetName;type["GetEntityCount"]=&Prefab::GetEntityCount;
 type["Instantiate"]=[](const Prefab& prefab,Scene& scene,sol::optional<Entity> parent){return prefab.Instantiate(scene,parent.value_or(Entity{}));};
 type["GetInstanceRoot"]=&Prefab::GetInstanceRoot;type["GetSource"]=&Prefab::GetSource;type["HasOverrides"]=&Prefab::HasOverrides;type["Refresh"]=&Prefab::Refresh;type["Revert"]=&Prefab::Revert;type["Unpack"]=&Prefab::Unpack;
}
}
