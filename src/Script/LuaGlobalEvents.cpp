#include "Script/LuaBindings.h"
#include "Bazzalt/GlobalEvent.h"
#include "Runtime/ApplicationEventsAccess.h"
namespace Bazzalt::Runtime {
namespace {
EventValue Value(EventArgumentType type,sol::object value){
    switch(type){
    case EventArgumentType::Boolean:if(value.get_type()==sol::type::boolean)return value.as<bool>();break;
    case EventArgumentType::Integer:if(value.is<std::int64_t>())return value.as<std::int64_t>();break;
    case EventArgumentType::Number:if(value.get_type()==sol::type::number)return value.as<double>();break;
    case EventArgumentType::String:if(value.get_type()==sol::type::string)return value.as<std::string>();break;
    case EventArgumentType::Vec2:if(value.is<Vec2>())return value.as<Vec2>();break;
    case EventArgumentType::Vec3:if(value.is<Vec3>())return value.as<Vec3>();break;
    case EventArgumentType::Vec4:if(value.is<Vec4>())return value.as<Vec4>();break;
    case EventArgumentType::Quaternion:if(value.is<Quaternion>())return value.as<Quaternion>();break;
    case EventArgumentType::UUID:if(value.is<UUID>())return value.as<UUID>();break;
    }throw std::invalid_argument("Global Event Lua argument has the wrong type");
}
sol::table Arguments(const std::vector<EventArgument>& arguments,sol::this_state state){sol::state_view lua(state);auto table=lua.create_table();for(std::size_t i=0;i<arguments.size();++i)table[i+1]=EventArgument(arguments[i]);return table;}
void Budget(lua_State* state,lua_Debug*){luaL_error(state,"Global Event callback exceeded instruction budget");}
}
void BindLuaGlobalEvents(sol::table api,Entity owner){
    api.new_enum("EventArgumentType","Boolean",EventArgumentType::Boolean,"Integer",EventArgumentType::Integer,"Number",EventArgumentType::Number,"String",EventArgumentType::String,"Vec2",EventArgumentType::Vec2,"Vec3",EventArgumentType::Vec3,"Vec4",EventArgumentType::Vec4,"Quaternion",EventArgumentType::Quaternion,"UUID",EventArgumentType::UUID);
    auto argument=api.new_usertype<EventArgument>("EventArgument");argument["Name"]=sol::readonly(&EventArgument::Name);argument["Type"]=sol::readonly(&EventArgument::Type);
    auto definition=api.new_usertype<EventDefinition>("EventDefinition");definition["Name"]=sol::readonly(&EventDefinition::Name);definition["Arguments"]=sol::property([](const EventDefinition& value,sol::this_state state){return Arguments(value.Arguments,state);});
    auto invocation=api.new_usertype<EventInvocation>("EventInvocation");invocation["Event"]=sol::property([](const EventInvocation& e){return UUID(e.Event);});invocation["Name"]=sol::readonly(&EventInvocation::Name);invocation["Arguments"]=sol::property([](const EventInvocation& e,sol::this_state state){return Arguments(e.Arguments,state);});
    invocation["Values"]=sol::property([](const EventInvocation& event,sol::this_state state){sol::state_view lua(state);auto values=lua.create_table();for(std::size_t i=0;i<event.Values.size();++i)values[i+1]=std::visit([&](const auto& v){return sol::make_object(state,std::decay_t<decltype(v)>(v));},event.Values[i]);return values;});
    invocation["GetValue"]=[](const EventInvocation& event,const std::string& name,sol::this_state state){return std::visit([&](const auto& v){return sol::make_object(state,std::decay_t<decltype(v)>(v));},event.GetValue(name));};
    auto type=api.new_usertype<GlobalEvent>("GlobalEvent",sol::constructors<GlobalEvent()>());type["Load"]=&GlobalEvent::Load;type["IsValid"]=&GlobalEvent::IsValid;type["GetAssetUUID"]=&GlobalEvent::GetAssetUUID;type["GetDefinition"]=&GlobalEvent::GetDefinition;type["Unsubscribe"]=&GlobalEvent::Unsubscribe;
    type["Fire"]=[](GlobalEvent event,sol::optional<sol::table> input){auto definition=event.GetDefinition();std::vector<EventValue> values;
        if(input){std::size_t count=0;for(auto pair:*input){if(pair.first.get_type()!=sol::type::string)throw std::invalid_argument("Use named Global Event arguments");++count;}if(count!=definition.Arguments.size())throw std::invalid_argument("Global Event argument count mismatch");}
        else if(!definition.Arguments.empty())throw std::invalid_argument("Missing event arguments");
        for(const auto& field:definition.Arguments){sol::object value=(*input)[field.Name];values.push_back(Value(field.Type,value));}event.Fire(std::move(values));};
    type["Subscribe"]=[owner](GlobalEvent event,sol::protected_function callback,sol::optional<Entity> entity){auto* state=callback.lua_state();auto previous=ApplicationEventsAccess::SetOwner(state);
        struct Restore{const void* Previous;~Restore(){ApplicationEventsAccess::SetOwner(Previous);}} restore{previous};
        return event.Subscribe([callback=std::move(callback),state](const EventInvocation& e)mutable{auto hook=lua_gethook(state);int mask=lua_gethookmask(state),count=lua_gethookcount(state);lua_sethook(state,Budget,LUA_MASKCOUNT,1000000);auto result=callback(EventInvocation(e));lua_sethook(state,hook,mask,count);if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}},entity.value_or(owner));};
}
}
