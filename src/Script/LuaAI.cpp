#include "Script/LuaBindings.h"
#include "Bazzalt/AI.h"
#include "Bazzalt/Systems/BehaviorTreeSystem.h"
#include "Bazzalt/Components/BehaviorTreeAgent.h"
#include <set>
namespace Bazzalt::Runtime {
namespace {
std::map<lua_State*,std::vector<Entity>> Owners;
std::set<lua_State*> Closing;
BehaviorTreeSystem& SystemFor(Entity entity){
    if(!entity||!entity.GetScene()->HasSystem<BehaviorTreeSystem>())throw std::invalid_argument("Expected a live AI agent scene");
    return entity.GetScene()->GetSystem<BehaviorTreeSystem>();
}
struct LuaBlackboard {
    Entity Agent;
    std::shared_ptr<AI::Blackboard> Owned=std::make_shared<AI::Blackboard>();
    AI::Blackboard& Resolve(){return Owned?*Owned:SystemFor(Agent).GetBlackboard(Agent);}
    static LuaBlackboard ForAgent(Entity entity){SystemFor(entity).GetBlackboard(entity);LuaBlackboard value;value.Agent=entity;value.Owned.reset();return value;}
};
struct Hook {
    lua_State* State;lua_Hook Previous;int Mask,Count;
    explicit Hook(lua_State* state):State(state),Previous(lua_gethook(state)),Mask(lua_gethookmask(state)),Count(lua_gethookcount(state)){
        lua_sethook(state,[](lua_State* s,lua_Debug*){luaL_error(s,"Lua AI instruction budget exceeded");},LUA_MASKCOUNT,1000000);
    }
    ~Hook(){lua_sethook(State,Previous,Mask,Count);}
};
AI::ActionHandler Action(sol::optional<sol::protected_function> fn,LuaBlackboard memory){
    if(!fn)return {};
    return [function=*fn,memory](const std::string& name,AI::Context& context,bool cancel) mutable {
        Hook budget(function.lua_state());auto result=function(name,context.Self,memory,context.DeltaTime,cancel);
        if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
        if(cancel)return AI::Status::Failure;
        if(result.get_type()!=sol::type::number)throw std::invalid_argument("AI action must return Bazzalt.BehaviorStatus");
        auto value=result.template get<double>();if(!std::isfinite(value)||value!=std::floor(value)||value<0||value>2)throw std::invalid_argument("Invalid AI action status");return static_cast<AI::Status>(int(value));
    };
}
AI::ConditionHandler Condition(sol::optional<sol::protected_function> fn,LuaBlackboard memory){
    if(!fn)return {};
    return [function=*fn,memory](const std::string& name,AI::Context& context) mutable {
        Hook budget(function.lua_state());auto result=function(name,context.Self,memory,context.DeltaTime);
        if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
        if(result.get_type()!=sol::type::boolean)throw std::invalid_argument("AI condition must return a boolean");return result.template get<bool>();
    };
}
}
void UnbindLuaAI(lua_State* state) noexcept {
    auto found=Owners.find(state);if(found==Owners.end())return;
    if(Closing.contains(state))return;
    Closing.insert(state);
    auto scenes=std::move(found->second);Owners.erase(found);
    for(auto root:scenes)if(root)try{SystemFor(root).ClearHandlers(state);}catch(const std::exception& error){std::cerr<<"[Lua AI] "<<error.what()<<'\n';}
    Closing.erase(state);
}
void BindLuaAI(sol::table api){
    api.new_enum("BehaviorStatus","Success",AI::Status::Success,"Failure",AI::Status::Failure,"Running",AI::Status::Running);
    auto board=api.new_usertype<LuaBlackboard>("Blackboard",sol::constructors<LuaBlackboard()>());
    board["Contains"]=[](LuaBlackboard& b,const std::string& key){return b.Resolve().Contains(key);};
    board["Erase"]=[](LuaBlackboard& b,const std::string& key){b.Resolve().Erase(key);};
    board["Number"]=[](LuaBlackboard& b,const std::string& key,sol::optional<double> fallback){return b.Resolve().Number(key,fallback.value_or(0));};
    board["Set"]=[](LuaBlackboard& b,const std::string& key,sol::object value){
        if(value.get_type()==sol::type::boolean)b.Resolve().Set(key,value.as<bool>());
        else if(value.get_type()==sol::type::number){double n=value.as<double>();if(!std::isfinite(n))throw std::invalid_argument("Non-finite AI number");b.Resolve().Set(key,n);}
        else if(value.get_type()==sol::type::string)b.Resolve().Set(key,value.as<std::string>());
        else if(value.is<Vec3>()){auto v=value.as<Vec3>();if(!std::isfinite(v.X)||!std::isfinite(v.Y)||!std::isfinite(v.Z))throw std::invalid_argument("Non-finite AI vector");b.Resolve().Set(key,v);}
        else throw std::invalid_argument("Blackboard expects boolean, number, string or Vec3");
    };
    board["Get"]=[](LuaBlackboard& b,const std::string& key,sol::this_state state)->sol::object{
        auto& values=b.Resolve().Values;auto found=values.find(key);if(found==values.end())return sol::make_object(state,sol::lua_nil);
        return std::visit([&](const auto& value){return sol::make_object(state,value);},found->second);
    };
    auto tree=api.new_usertype<AI::BehaviorTree>("BehaviorTree",sol::no_constructor);
    tree["FromSource"]=&AI::BehaviorTree::FromSource;tree["Load"]=&AI::BehaviorTree::Load;
    tree["Initialize"]=[](const AI::BehaviorTree& tree,LuaBlackboard& b){tree.Initialize(b.Resolve());};
    tree["Tick"]=[](AI::BehaviorTree& tree,Entity self,LuaBlackboard b,float dt,sol::optional<sol::protected_function> action,sol::optional<sol::protected_function> condition){AI::Context context{self,b.Resolve(),dt,Action(action,b),Condition(condition,b)};return tree.Tick(context);};
    tree["Reset"]=[](AI::BehaviorTree& tree,Entity self,LuaBlackboard b,sol::optional<sol::protected_function> action){AI::Context context{self,b.Resolve(),0,Action(action,b),{}};tree.Reset(context);};
    auto component=api.new_usertype<BehaviorTreeAgent>("BehaviorTreeAgent",sol::constructors<BehaviorTreeAgent()>(),sol::base_classes,sol::bases<Component>());
    component["Tree"]=&BehaviorTreeAgent::Tree;component["Loop"]=&BehaviorTreeAgent::Loop;component["IsValid"]=&BehaviorTreeAgent::IsValid;
    sol::table operations=api["_ComponentOperations"];auto op=operations.create_named("BehaviorTreeAgent");
    op["Has"]=[](Entity e){return e.HasComponent<BehaviorTreeAgent>();};op["Get"]=[](Entity e){return BehaviorTreeAgent(e.GetComponent<BehaviorTreeAgent>());};
    op["Add"]=[](Entity e){return BehaviorTreeAgent(e.AddComponent<BehaviorTreeAgent>());};op["Set"]=[](Entity e,BehaviorTreeAgent value){e.GetComponent<BehaviorTreeAgent>()=value;};
    op["Remove"]=[](Entity e){SystemFor(e).Reset(e);e.RemoveComponent<BehaviorTreeAgent>();};op["Inherited"]=[](Entity e)->std::optional<BehaviorTreeAgent>{if(!e)return {};auto value=e.GetScene()->TryGetInheritedComponent<BehaviorTreeAgent>(e);return value?std::optional<BehaviorTreeAgent>(*value):std::nullopt;};
    op["IsEnabled"]=[](Entity e){return e.IsComponentEnabled<BehaviorTreeAgent>();};op["SetEnabled"]=[](Entity e,bool enabled){e.SetComponentEnabled<BehaviorTreeAgent>(enabled);};
    auto ai=api.create_named("AI");
    ai["GetBlackboard"]=&LuaBlackboard::ForAgent;
    ai["Reset"]=[](Entity e){SystemFor(e).Reset(e);};
    ai["SetHandlers"]=[state=api.lua_state()](Entity e,sol::optional<sol::protected_function> action,sol::optional<sol::protected_function> condition){
        if(Closing.contains(state))throw std::logic_error("Cannot register AI handlers while this Lua VM is closing");
        auto memory=LuaBlackboard::ForAgent(e);auto root=e.GetScene()->GetRootEntity();auto& roots=Owners[state];if(std::find(roots.begin(),roots.end(),root)==roots.end())roots.push_back(root);
        SystemFor(e).SetHandlers(e,Action(action,memory),Condition(condition,memory),state);
    };
    ai["ClearHandlers"]=[](Entity e){SystemFor(e).SetHandlers(e,{},{},nullptr);};
}
}
