#include "Script/LuaBindings.h"
#include "Bazzalt/Components/LOD.h"
#include "Rendering/AdvancedLODFields.h"
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
    api.new_enum("LODEasing","Linear",LODEasing::Linear,"SmoothStep",LODEasing::SmoothStep);
    api.new_enum("ImpostorFacing","Spherical",ImpostorFacing::Spherical,"Cylindrical",ImpostorFacing::Cylindrical);
    auto cluster=api.new_usertype<HLOD>("HLOD",sol::constructors<HLOD()>(),sol::base_classes,sol::bases<Component>());
    cluster["Center"]=&HLOD::Center;cluster["Size"]=&HLOD::Size;cluster["Bias"]=&HLOD::Bias;cluster["Hysteresis"]=&HLOD::Hysteresis;cluster["ScreenHeight"]=&HLOD::ScreenHeight;cluster["CullHeight"]=&HLOD::CullHeight;cluster["ForcedLevel"]=&HLOD::ForcedLevel;
    cluster["Sources"]=sol::property([](const HLOD& v){return sol::as_table(v.Sources);},[](HLOD& v,sol::as_table_t<std::vector<UUID>> roots){v.Sources=std::move(roots.value());});
    cluster["Proxies"]=sol::property([](const HLOD& v){return sol::as_table(v.Proxies);},[](HLOD& v,sol::as_table_t<std::vector<UUID>> roots){v.Proxies=std::move(roots.value());});
    cluster["IsValid"]=&HLOD::IsValid;cluster["IsConfigured"]=&HLOD::IsConfigured;cluster["GetLOD"]=&HLOD::GetLOD;cluster["ForceLevel"]=&HLOD::ForceLevel;
    auto fade=api.new_usertype<LODTransition>("LODTransition",sol::constructors<LODTransition()>(),sol::base_classes,sol::bases<Component>());
    fade["Duration"]=&LODTransition::Duration;fade["Easing"]=&LODTransition::Easing;fade["FadeCulling"]=&LODTransition::FadeCulling;fade["IsValid"]=&LODTransition::IsValid;fade["Evaluate"]=&LODTransition::Evaluate;
    auto impostor=api.new_usertype<Impostor>("Impostor",sol::constructors<Impostor()>(),sol::base_classes,sol::bases<Component>());
    impostor["Atlas"]=&Impostor::Atlas;impostor["Size"]=&Impostor::Size;impostor["Pivot"]=&Impostor::Pivot;impostor["Center"]=&Impostor::Center;impostor["Color"]=&Impostor::Color;impostor["Columns"]=&Impostor::Columns;impostor["Rows"]=&Impostor::Rows;impostor["ViewCount"]=&Impostor::ViewCount;impostor["AzimuthOffset"]=&Impostor::AzimuthOffset;impostor["AlphaCutoff"]=&Impostor::AlphaCutoff;impostor["Facing"]=&Impostor::Facing;impostor["LayerMask"]=&Impostor::LayerMask;impostor["Visible"]=&Impostor::Visible;impostor["CastShadows"]=&Impostor::CastShadows;
    impostor["IsValid"]=&Impostor::IsValid;impostor["SelectFrame"]=&Impostor::SelectFrame;impostor["GetAtlasRect"]=&Impostor::GetAtlasRect;impostor["GetMatrix"]=&Impostor::GetMatrix;
    const auto registerOperations=[&]<class T>(const char* name){auto entry=operations.create_named(name);
        entry["Has"]=[](Entity e){return e.HasComponent<T>();};entry["Get"]=[](Entity e){return T(e.GetComponent<T>());};entry["Add"]=[](Entity e){return T(e.AddComponent<T>());};
        entry["Set"]=[](Entity e,T value){if(!value.IsValid())throw std::invalid_argument("Invalid advanced LOD configuration");e.GetComponent<T>()=std::move(value);};entry["Remove"]=[](Entity e){e.RemoveComponent<T>();};entry["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<T>();};entry["SetEnabled"]=[](Entity e,bool enabled){e.SetComponentEnabled<T>(enabled);};entry["Inherited"]=[](Entity)->std::optional<T>{return {};};
    };
    registerOperations.template operator()<HLOD>("HLOD");registerOperations.template operator()<LODTransition>("LODTransition");registerOperations.template operator()<Impostor>("Impostor");

}
}
