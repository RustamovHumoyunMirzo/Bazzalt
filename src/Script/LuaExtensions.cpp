#include "Script/LuaBindings.h"
#include "Runtime/ExtensionAccess.h"
#include <fstream>
#include <set>
namespace Bazzalt::Runtime {
ExtensionValue LuaExtensionValue(sol::object value){
    if(value==sol::nil)return {};if(value.is<bool>())return value.as<bool>();
    if(value.get_type()==sol::type::number){if(value.is<std::int64_t>())return value.as<std::int64_t>();return value.as<double>();}
    if(value.is<std::string>())return value.as<std::string>();if(value.is<Vec2>())return value.as<Vec2>();if(value.is<Vec3>())return value.as<Vec3>();if(value.is<Vec4>())return value.as<Vec4>();if(value.is<UUID>())return value.as<UUID>();throw std::invalid_argument("Unsupported extension argument type");
}
namespace {
sol::object Require(sol::table api,sol::table cache,std::shared_ptr<std::set<std::string>> loading,const std::string& name,const std::string& caller){
    sol::state_view lua(api.lua_state());
    if(!ExtensionAccess::Visible(name,caller))throw std::runtime_error("Private or unavailable extension module: "+name);
    sol::object existing=cache[name];if(existing.valid()&&existing!=sol::nil)return existing;
    if(!loading->insert(name).second)throw std::runtime_error("Extension require cycle: "+name);
    struct Guard{std::shared_ptr<std::set<std::string>> Set;std::string Name;~Guard(){Set->erase(Name);}} guard{loading,name};
    const auto owner=ExtensionAccess::Owner(name);auto path=ExtensionAccess::LuaModule(name);
    if(!path.empty()){
        if(std::filesystem::file_size(path)>4*1024*1024)throw std::length_error("Extension Lua module exceeds limit");
        std::ifstream stream(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(stream)),{});
        auto loaded=lua.load(bytes,"@"+name,sol::load_mode::binary);if(!loaded.valid()){sol::error failure=loaded;throw std::runtime_error(failure.what());}
        sol::environment environment(lua,sol::create,lua.globals());auto localApi=lua.create_table();for(auto pair:api)localApi[pair.first]=pair.second;
        localApi.set_function("require",[api,cache,loading,owner](sol::variadic_args args)->sol::object{auto offset=args.size()&&args[0].get_type()==sol::type::table?1u:0u;if(args.size()!=offset+1)throw std::invalid_argument("Bazzalt.require expects one module name");return Require(api,cache,loading,args[offset].as<std::string>(),owner);});
        environment["Bazzalt"]=localApi;sol::protected_function program=loaded;sol::set_environment(environment,program);auto result=program();
        if(!result.valid()){sol::error failure=result;throw std::runtime_error(failure.what());}
        if(result.get_type()!=sol::type::table)throw std::runtime_error("Extension module must return a table: "+name);
        auto module=result.get<sol::table>();cache[name]=module;return sol::make_object(lua,module);
    }
    auto names=ExtensionAccess::Functions(name);if(names.empty())throw std::runtime_error("Extension module is unavailable: "+name);
    auto module=lua.create_table();
    for(auto function:names)module.set_function(function,[name,function,lua,owner](sol::variadic_args values)->sol::object{
        if(values.size()>128)throw std::length_error("Too many extension arguments");ExtensionArguments arguments;for(auto value:values)arguments.push_back(LuaExtensionValue(sol::object(value)));
        struct Scope{std::string Previous;~Scope(){ExtensionAccess::SetCaller(std::move(Previous));}} scope{ExtensionAccess::SetCaller(owner)};
        auto result=Extensions::Call(name,function,arguments);return std::visit([&](const auto& value)->sol::object{using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,std::monostate>)return sol::make_object(lua,sol::nil);else return sol::make_object(lua,value);},result);
    });
    cache[name]=module;return sol::make_object(lua,module);
}
}
sol::table ExtensionCache(sol::state_view lua){sol::object cached=lua.registry()["Bazzalt.ExtensionCache"];if(cached.is<sol::table>())return cached.as<sol::table>();auto cache=lua.create_table();lua.registry()["Bazzalt.ExtensionCache"]=cache;return cache;}
sol::object RequireLuaExtension(sol::table api,const std::string& name,const std::string& caller){return Require(api,ExtensionCache(sol::state_view(api.lua_state())),std::make_shared<std::set<std::string>>(),name,caller);}
void BindLuaExtensionsForOwner(sol::table api,const std::string& owner){
    sol::state_view lua(api.lua_state());auto cache=ExtensionCache(lua);auto loading=std::make_shared<std::set<std::string>>();
    api.set_function("require",[api,cache,loading,owner](sol::variadic_args args)->sol::object{auto offset=args.size()&&args[0].get_type()==sol::type::table?1u:0u;if(args.size()!=offset+1)throw std::invalid_argument("Bazzalt.require expects one module name");return Require(api,cache,loading,args[offset].as<std::string>(),owner);});
    auto service=api.create_named("Extensions");service["GetAsset"]=sol::overload([](const std::string& e,const std::string& a){return Extensions::GetAsset(e,a);},[](sol::table,const std::string& e,const std::string& a){return Extensions::GetAsset(e,a);});service["HasModule"]=sol::overload([](const std::string& name){return Extensions::HasModule(name);},[](sol::table,const std::string& name){return Extensions::HasModule(name);});service["GetModules"]=sol::overload([](){return sol::as_table(Extensions::GetModules());},[](sol::table){return sol::as_table(Extensions::GetModules());});
    service["Call"]=[](sol::variadic_args values,sol::this_state state)->sol::object{
        auto offset=values.size()&&values[0].get_type()==sol::type::table?1u:0u;
        if(values.size()<offset+2||values.size()>offset+3)throw std::invalid_argument("Extensions.Call expects module, function and optional argument array");
        ExtensionArguments arguments;if(values.size()>offset+2){auto table=values[offset+2].as<sol::table>();if(table.size()>128)throw std::length_error("Too many extension arguments");for(std::size_t i=1;i<=table.size();++i)arguments.push_back(LuaExtensionValue(table.get<sol::object>(i)));}
        auto result=Extensions::Call(values[offset].as<std::string>(),values[offset+1].as<std::string>(),arguments);return std::visit([&](const auto& v)->sol::object{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::monostate>)return sol::make_object(state,sol::nil);else return sol::make_object(state,v);},result);
    };

}
void BindLuaExtensions(sol::table api){BindLuaExtensionsForOwner(api,"");}

}
