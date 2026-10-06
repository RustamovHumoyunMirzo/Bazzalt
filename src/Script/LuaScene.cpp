#include "Script/LuaBindings.h"
#include "Script/LuaSystems.h"
#include "Bazzalt/EntityReference.h"
namespace Bazzalt::Runtime {
void BindLuaScene(sol::table api){
    auto reference=api.new_usertype<EntityReference>("EntityReference",sol::constructors<EntityReference(),EntityReference(UUID)>());
    reference["GetUUID"]=&EntityReference::GetUUID;reference["Resolve"]=&EntityReference::Resolve;reference["IsValid"]=&EntityReference::IsValid;reference["SetWorldTransform"]=&EntityReference::SetWorldTransform;
    reference["TryGetWorldTransform"]=[](EntityReference value){Transform transform;bool found=value.TryGetWorldTransform(transform);return std::make_tuple(found,transform);};
    sol::table operations=api["_ComponentOperations"];
    auto entity=api.new_usertype<Entity>("Entity",sol::constructors<Entity()>());
    entity["IsValid"]=&Entity::IsValid;entity["GetId"]=&Entity::GetId;entity["GetUUID"]=&Entity::GetUUID;entity["GetScene"]=&Entity::GetScene;
    entity["GetParent"]=&Entity::GetParent;entity["HasParent"]=&Entity::HasParent;
    entity["GetChildren"]=[](Entity value){return sol::as_table(value.GetChildren());};entity["IsAncestorOf"]=&Entity::IsAncestorOf;
    entity["GetWorldMatrix"]=&Entity::GetWorldMatrix;entity["GetWorldTransform"]=&Entity::GetWorldTransform;entity["SetWorldTransform"]=&Entity::SetWorldTransform;
    entity["SetParent"]=[](Entity value,Entity parent,sol::optional<bool> stays){return value.SetParent(parent,stays.value_or(true));};
    entity["AddChild"]=[](Entity value,Entity child,sol::optional<bool> stays){return value.AddChild(child,stays.value_or(true));};
    entity["RemoveParent"]=[](Entity value,sol::optional<bool> stays){return value.RemoveParent(stays.value_or(true));};
    for(auto name:{"HasComponent","GetComponent","AddComponent","RemoveComponent","IsComponentEnabled","SetComponentEnabled","SetComponent"}){
        std::string operation=std::string(name)=="HasComponent"?"Has":std::string(name)=="GetComponent"?"Get":std::string(name)=="AddComponent"?"Add":std::string(name)=="RemoveComponent"?"Remove":std::string(name)=="IsComponentEnabled"?"IsEnabled":std::string(name)=="SetComponentEnabled"?"SetEnabled":"Set";
        entity[name]=[operations,operation](Entity owner,const std::string& type,sol::variadic_args arguments,sol::this_state state)->sol::object{
            sol::object found=operations[type];if(!found.is<sol::table>())throw std::invalid_argument("Unknown component type: "+type);
            sol::protected_function method=found.as<sol::table>()[operation];
            auto result=method(owner,sol::as_args(arguments));if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
            return result.return_count()?result.get<sol::object>():sol::make_object(state,sol::nil);
        };
    }
    entity[sol::meta_function::equal_to]=[](Entity a,Entity b){return a==b;};
    entity["TryGetComponent"]=[operations](Entity value,const std::string& type,sol::this_state state)->sol::object{
        sol::object found=operations[type];if(!found.is<sol::table>())throw std::invalid_argument("Unknown component type");
        sol::table entry=found;sol::function has=entry["Has"];if(!has(value).get<bool>())return sol::make_object(state,sol::nil);
        sol::function get=entry["Get"];return get(value).get<sol::object>();
    };
    entity["AddOrReplaceComponent"]=[operations](Entity value,const std::string& type,sol::object component){
        sol::object found=operations[type];if(!found.is<sol::table>())throw std::invalid_argument("Unknown component type");
        sol::table entry=found;sol::function has=entry["Has"];if(!has(value).get<bool>()){sol::function add=entry["Add"];add(value);}
        sol::function set=entry["Set"];set(value,component);sol::function get=entry["Get"];return get(value).get<sol::object>();
    };
    auto scene=api.new_usertype<Scene>("Scene",sol::no_constructor);
    scene["CreateEntity"]=sol::overload([](Scene& s,sol::optional<std::string> name){return s.CreateEntity(name.value_or(""));},[](Scene& s,UUID id,sol::optional<std::string> name){return s.CreateEntity(id,name.value_or(""));});
    scene["DestroyEntity"]=&Scene::DestroyEntity;scene["Clear"]=&Scene::Clear;scene["FindEntityByName"]=&Scene::FindEntityByName;
    scene["GetEntities"]=[operations](Scene& value,sol::optional<std::string> component){
        std::vector<Entity> result;
        for(auto handle:value.GetRegistry().view<Identity>()){
            auto entity=value.GetEntity(static_cast<Entity::Id>(handle));
            if(component){sol::object type=operations[*component];if(!type.is<sol::table>())throw std::invalid_argument("Unknown component type");sol::function has=type.as<sol::table>()["Has"];if(!has(entity).get<bool>())continue;}
            result.push_back(entity);
        }return sol::as_table(std::move(result));
    };
    scene["AddSystem"]=[](Scene& value,const std::string& name,sol::table object){
        auto& systems=value.AddSystem<LuaSystems>();if(systems.Updating)throw std::logic_error("Cannot add systems during system updates");
        if(systems.Entries.contains(name))throw std::logic_error("Duplicate Lua system");
        LuaSystems::Invoke(object,"OnCreate",value,0,false);systems.Entries.emplace(name,LuaSystems::Entry{object});return object;
    };
    scene["HasSystem"]=[](Scene& value,const std::string& name){return value.HasSystem<LuaSystems>()&&value.GetSystem<LuaSystems>().Entries.contains(name);};
    scene["GetSystem"]=[](Scene& value,const std::string& name){return value.GetSystem<LuaSystems>().Entries.at(name).Object;};
    scene["RemoveSystem"]=[](Scene& value,const std::string& name){
        if(!value.HasSystem<LuaSystems>())return false;auto& systems=value.GetSystem<LuaSystems>();if(systems.Updating)throw std::logic_error("Cannot remove systems during system updates");
        auto found=systems.Entries.find(name);if(found==systems.Entries.end())return false;LuaSystems::Invoke(found->second.Object,"OnDestroy",value,0,false);systems.Entries.erase(found);return true;
    };
    scene["SetSystemEnabled"]=[](Scene& value,const std::string& name,bool enabled){value.GetSystem<LuaSystems>().Entries.at(name).Enabled=enabled;};
    scene["IsSystemEnabled"]=[](Scene& value,const std::string& name){return value.GetSystem<LuaSystems>().Entries.at(name).Enabled;};
    scene["GetEntity"]=sol::overload([](Scene& s,UUID id){return s.GetEntity(id);},[](Scene& s,Entity::Id id){return s.GetEntity(id);});
    scene["GetRootEntity"]=&Scene::GetRootEntity;scene["GetUUID"]=&Scene::GetUUID;scene["GetEntityCount"]=&Scene::GetEntityCount;
    scene["GetEnvironment"]=[](Scene& s){return SceneEnvironment(s.GetEnvironment());};scene["SetEnvironment"]=&Scene::SetEnvironment;
    scene["SetParent"]=[](Scene& s,Entity c,Entity p,sol::optional<bool> stays){return s.SetParent(c,p,stays.value_or(true));};
    scene["RemoveParent"]=[](Scene& s,Entity c,sol::optional<bool> stays){return s.RemoveParent(c,stays.value_or(true));};
    scene["GetParent"]=&Scene::GetParent;scene["GetChildren"]=[](Scene& s,Entity p){return sol::as_table(s.GetChildren(p));};scene["IsAncestor"]=&Scene::IsAncestor;
    scene["GetWorldMatrix"]=&Scene::GetWorldMatrix;scene["GetWorldTransform"]=&Scene::GetWorldTransform;scene["SetWorldTransform"]=&Scene::SetWorldTransform;
    scene["InstantiateModel"]=sol::overload([](Scene& s,UUID id,sol::optional<Entity> p,sol::optional<std::string> n){return s.InstantiateModel(id,p.value_or(Entity{}),n.value_or(""));},[](Scene& s,const ModelAsset& asset,sol::optional<Entity> p,sol::optional<std::string> n){return s.InstantiateModel(asset,p.value_or(Entity{}),n.value_or(""));});
    scene["ScreenPointToRay"]=&Scene::ScreenPointToRay;
    scene["TryGetInheritedComponent"]=[operations](Scene&,Entity value,const std::string& type){
        sol::object found=operations[type];if(!found.is<sol::table>())throw std::invalid_argument("Unknown component type");
        sol::function function=found.as<sol::table>()["Inherited"];return function(value).get<sol::object>();
    };
    scene["Pick"]=[](Scene& s,Entity c,Vec2 point,Vec2 size,sol::optional<SceneQueryOptions> options){return s.Pick(c,point,size,options.value_or(SceneQueryOptions{}));};
    scene["PickAll"]=[](Scene& s,Entity c,Vec2 point,Vec2 size,sol::optional<SceneQueryOptions> options){return sol::as_table(s.PickAll(c,point,size,options.value_or(SceneQueryOptions{})));};
    scene["Raycast"]=[](Scene& s,SceneRay ray,sol::optional<SceneQueryOptions> options){return s.Raycast(ray,options.value_or(SceneQueryOptions{}));};
    scene["RaycastAll"]=[](Scene& s,SceneRay ray,sol::optional<SceneQueryOptions> options){return sol::as_table(s.RaycastAll(ray,options.value_or(SceneQueryOptions{})));};
    scene["OverlapSphere"]=[](Scene& s,Vec3 p,float r,sol::optional<SceneQueryOptions> o){return sol::as_table(s.OverlapSphere(p,r,o.value_or(SceneQueryOptions{})));};
    scene["OverlapBox"]=[](Scene& s,Vec3 p,Vec3 e,sol::optional<SceneQueryOptions> o){return sol::as_table(s.OverlapBox(p,e,o.value_or(SceneQueryOptions{})));};
    auto ray=api.new_usertype<SceneRay>("SceneRay",sol::constructors<SceneRay()>());ray["Origin"]=&SceneRay::Origin;ray["Direction"]=&SceneRay::Direction;
    auto query=api.new_usertype<SceneQueryOptions>("SceneQueryOptions",sol::constructors<SceneQueryOptions()>());query["LayerMask"]=&SceneQueryOptions::LayerMask;query["MaxDistance"]=&SceneQueryOptions::MaxDistance;query["IncludeDisabled"]=&SceneQueryOptions::IncludeDisabled;
    query["Filter"]=&SceneQueryOptions::Filter;
    auto hit=api.new_usertype<SceneQueryHit>("SceneQueryHit",sol::constructors<SceneQueryHit()>());hit["Target"]=&SceneQueryHit::Target;hit["Point"]=&SceneQueryHit::Point;hit["Normal"]=&SceneQueryHit::Normal;hit["Distance"]=&SceneQueryHit::Distance;hit["StartedInside"]=&SceneQueryHit::StartedInside;
    auto manager=api.create_named("SceneManager");manager["GetActiveScene"]=&SceneManager::GetActiveScene;manager["IsLoadPending"]=&SceneManager::IsLoadPending;manager["GetLastError"]=&SceneManager::GetLastError;
    manager["LoadScene"]=sol::overload([](const std::string& path){return SceneManager::LoadScene(std::filesystem::u8path(path));},[](UUID id){return SceneManager::LoadScene(id);});
}
}
