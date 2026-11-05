#include "Script/LuaBindings.h"
#include "Bazzalt/DeveloperConsole.h"
#include "Runtime/ApplicationEventsAccess.h"
#include <stdexcept>
namespace Bazzalt::Runtime {
namespace {
CVarValue Value(sol::object value){
    if(value.get_type()==sol::type::boolean)return value.as<bool>();
    if(value.get_type()==sol::type::string)return value.as<std::string>();
    if(value.get_type()==sol::type::number){value.push();bool integer=lua_isinteger(value.lua_state(),-1);lua_pop(value.lua_state(),1);if(integer)return value.as<std::int64_t>();return value.as<double>();}
    throw std::invalid_argument("CVar value must be Boolean, integer, float or string");
}
sol::object Object(sol::this_state state,const CVarValue& value){return std::visit([&](const auto& v){return sol::make_object(state,v);},value);}
struct OwnerScope {const void* Old;explicit OwnerScope(lua_State* state):Old(ApplicationEventsAccess::SetOwner(state)){}~OwnerScope(){ApplicationEventsAccess::SetOwner(Old);}};
void Budget(lua_State* state,lua_Debug*){luaL_error(state,"Developer console callback exceeded its Lua instruction budget");}
struct Hook {lua_State* State;lua_Hook Old;int Mask,Count;explicit Hook(lua_State* s):State(s),Old(lua_gethook(s)),Mask(lua_gethookmask(s)),Count(lua_gethookcount(s)){lua_sethook(s,Budget,LUA_MASKCOUNT,1000000);}~Hook(){lua_sethook(State,Old,Mask,Count);}};
CVar::Callback Changed(sol::protected_function fn){auto* state=fn.lua_state();return [fn=std::move(fn),state](const CVarChange& change)mutable{Hook hook(state);auto result=fn(CVarChange(change));if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}};}
}
void BindLuaDeveloperConsole(sol::table api){
    api.new_enum<CVarType>("CVarType",{{"Boolean",CVarType::Boolean},{"Integer",CVarType::Integer},{"Float",CVarType::Float},{"String",CVarType::String}});
    api.new_enum<CVarFlags>("CVarFlags",{{"None",CVarFlags::None},{"ReadOnly",CVarFlags::ReadOnly},{"Archive",CVarFlags::Archive},{"Cheat",CVarFlags::Cheat},{"RestartRequired",CVarFlags::RestartRequired}});
    auto definition=api.new_usertype<CVarDefinition>("CVarDefinition",sol::constructors<CVarDefinition()>());definition["Name"]=&CVarDefinition::Name;definition["Description"]=&CVarDefinition::Description;definition["Flags"]=&CVarDefinition::Flags;definition["Minimum"]=&CVarDefinition::Minimum;definition["Maximum"]=&CVarDefinition::Maximum;
    definition["DefaultValue"]=sol::property([](const CVarDefinition& d,sol::this_state s){return Object(s,d.DefaultValue);},[](CVarDefinition& d,sol::object v){d.DefaultValue=Value(v);});
    auto info=api.new_usertype<CVarInfo>("CVarInfo",sol::no_constructor);info["Definition"]=&CVarInfo::Definition;info["Type"]=&CVarInfo::Type;info["Value"]=sol::property([](const CVarInfo& d,sol::this_state s){return Object(s,d.Value);});info["PendingValue"]=sol::property([](const CVarInfo& d,sol::this_state s){return d.PendingValue?Object(s,*d.PendingValue):sol::make_object(s,sol::lua_nil);});
    auto change=api.new_usertype<CVarChange>("CVarChange",sol::no_constructor);change["Name"]=&CVarChange::Name;change["Pending"]=&CVarChange::Pending;change["Previous"]=sol::property([](const CVarChange& d,sol::this_state s){return Object(s,d.Previous);});change["Value"]=sol::property([](const CVarChange& d,sol::this_state s){return Object(s,d.Value);});
    auto variables=api.create_named("CVar");
    variables["Register"]=[](CVarDefinition d,sol::optional<sol::protected_function> f,sol::this_state state){OwnerScope owner(state);CVar::Register(std::move(d),f?Changed(std::move(*f)):CVar::Callback{});};
    variables["Unregister"]=&CVar::Unregister;variables["Exists"]=&CVar::Exists;
    variables["GetValue"]=[](const std::string& name,sol::this_state state){return Object(state,CVar::GetValue(name));};variables["GetInfo"]=&CVar::GetInfo;
    variables["GetAll"]=[](sol::optional<std::string> p){return sol::as_table(CVar::GetAll(p.value_or("")));};
    variables["SetValue"]=[](const std::string& name,sol::object v){auto value=Value(v);const auto type=CVar::GetInfo(name).Type;if(type==CVarType::Float&&std::holds_alternative<std::int64_t>(value))value=double(std::get<std::int64_t>(value));CVar::SetValue(name,std::move(value));};
    variables["SetFromString"]=&CVar::SetFromString;variables["Reset"]=&CVar::Reset;variables["Toggle"]=&CVar::Toggle;variables["ApplyPendingChanges"]=&CVar::ApplyPendingChanges;
    variables["Subscribe"]=[](sol::protected_function f){OwnerScope owner(f.lua_state());return CVar::Subscribe(Changed(std::move(f)));};variables["Unsubscribe"]=&CVar::Unsubscribe;
    variables["ExportArchive"]=[]{return sol::as_table(CVar::ExportArchive());};variables["ImportArchive"]=&CVar::ImportArchive;variables["ToString"]=[](sol::object value){return CVar::ToString(Value(value));};
    auto command=api.new_usertype<ConsoleCommand>("ConsoleCommand",sol::constructors<ConsoleCommand()>());command["Name"]=&ConsoleCommand::Name;command["Description"]=&ConsoleCommand::Description;command["Usage"]=&ConsoleCommand::Usage;command["Cheat"]=&ConsoleCommand::Cheat;
    auto result=api.new_usertype<ConsoleResult>("ConsoleResult",sol::no_constructor);result["Success"]=&ConsoleResult::Success;result["Output"]=&ConsoleResult::Output;
    auto console=api.create_named("DeveloperConsole");console["SetEnabled"]=&DeveloperConsole::SetEnabled;console["IsEnabled"]=&DeveloperConsole::IsEnabled;console["SetVisible"]=&DeveloperConsole::SetVisible;console["IsVisible"]=&DeveloperConsole::IsVisible;console["SetCheatsAllowed"]=&DeveloperConsole::SetCheatsAllowed;console["AreCheatsAllowed"]=&DeveloperConsole::AreCheatsAllowed;
    console["RegisterCommand"]=[](ConsoleCommand d,sol::protected_function fn){auto* state=fn.lua_state();OwnerScope owner(state);return DeveloperConsole::RegisterCommand(std::move(d),[fn=std::move(fn),state](const std::vector<std::string>& args)mutable{Hook hook(state);auto result=fn(sol::as_table(args));if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}if(result.return_count()==0||result.get_type()==sol::type::nil)return std::string{};return result.get<std::string>();});};
    console["UnregisterCommand"]=&DeveloperConsole::UnregisterCommand;console["Execute"]=&DeveloperConsole::Execute;console["Complete"]=[](const std::string& p){return sol::as_table(DeveloperConsole::Complete(p));};console["GetCommands"]=[](sol::optional<std::string> p){return sol::as_table(DeveloperConsole::GetCommands(p.value_or("")));};
    console["GetHistory"]=[]{return sol::as_table(DeveloperConsole::GetHistory());};console["ClearHistory"]=&DeveloperConsole::ClearHistory;console["SetHistoryLimit"]=&DeveloperConsole::SetHistoryLimit;console["SetMessageLimit"]=&DeveloperConsole::SetMessageLimit;
    console["Log"]=[](const std::string& text,sol::optional<ConsoleLevel> level,sol::optional<std::string> source){DeveloperConsole::Log(text,level.value_or(ConsoleLevel::Info),source.value_or("Script"));};console["GetMessages"]=[](sol::optional<ConsoleFilter> filter){return sol::as_table(DeveloperConsole::GetMessages(filter.value_or(ConsoleFilter{})));};console["Clear"]=[](sol::optional<ConsoleFilter> filter){DeveloperConsole::Clear(filter.value_or(ConsoleFilter{}));};
}
}
