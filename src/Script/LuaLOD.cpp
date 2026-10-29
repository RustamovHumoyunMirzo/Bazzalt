#include "Script/LuaBindings.h"
#include "Bazzalt/Components/LOD.h"
namespace Bazzalt::Runtime {
void BindLuaLOD(sol::table api){
    auto level=api.new_usertype<LODLevel>("LODLevel",sol::constructors<LODLevel()>());
    level["ScreenHeight"]=&LODLevel::ScreenHeight;
    level["Roots"]=sol::property([](const LODLevel& v){return sol::as_table(v.Roots);},[](LODLevel& v,sol::as_table_t<std::vector<UUID>> roots){v.Roots=std::move(roots.value());});
    auto type=api.new_usertype<LOD>("LOD",sol::constructors<LOD()>(),sol::base_classes,sol::bases<Component>());
    type["Center"]=&LOD::Center;type["Size"]=&LOD::Size;type["Bias"]=&LOD::Bias;type["Hysteresis"]=&LOD::Hysteresis;type["Cull"]=&LOD::Cull;type["ForcedLevel"]=&LOD::ForcedLevel;
    type["Levels"]=sol::property([](const LOD& v){return sol::as_table(v.Levels);},[](LOD& v,sol::as_table_t<std::vector<LODLevel>> levels){v.Levels=std::move(levels.value());});
    type["ForceLevel"]=&LOD::ForceLevel;type["IsValid"]=&LOD::IsValid;
    type["SelectLevel"]=[](const LOD& v,float height,sol::optional<int> previous){return v.SelectLevel(height,previous.value_or(-2));};
    type["GetScreenHeight"]=[](const LOD& v,Mat4 world,Vec3 eye,float projection,sol::optional<bool> orthographic){return v.GetScreenHeight(world,eye,projection,orthographic.value_or(false));};
    sol::table operations=api["_ComponentOperations"];auto value=operations.create_named("LOD");
    value["Has"]=[](Entity e){return e.HasComponent<LOD>();};value["Get"]=[](Entity e){return LOD(e.GetComponent<LOD>());};value["Add"]=[](Entity e){return LOD(e.AddComponent<LOD>());};
    value["Set"]=[](Entity e,LOD v){if(!v.IsValid())throw std::invalid_argument("Invalid LOD configuration");e.GetComponent<LOD>()=std::move(v);};
    value["Remove"]=[](Entity e){e.RemoveComponent<LOD>();};value["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<LOD>();};value["SetEnabled"]=[](Entity e,bool enabled){e.SetComponentEnabled<LOD>(enabled);};
    value["Inherited"]=[](Entity)->std::optional<LOD>{return {};};
}
}
