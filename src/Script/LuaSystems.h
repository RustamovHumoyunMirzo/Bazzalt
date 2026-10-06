#pragma once
#include "Script/LuaBindings.h"
namespace Bazzalt::Runtime {
class LuaSystems final : public System {
public:
    struct Entry { sol::table Object;bool Enabled=true; };
    std::map<std::string,Entry> Entries;
    bool Updating=false;
    static void Invoke(sol::table object,const char* name,Scene& scene,float dt,bool timed){
        sol::object value=object[name];if(!value.valid()||value==sol::nil)return;
        sol::protected_function function=value;
        lua_sethook(object.lua_state(),[](lua_State* state,lua_Debug*){luaL_error(state,"Lua system instruction budget exceeded");},LUA_MASKCOUNT,1000000);
        auto result=timed?function(object,std::ref(scene),dt):function(object,std::ref(scene));
        lua_sethook(object.lua_state(),nullptr,0,0);
        if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
    }
protected:
    void Run(Scene& scene,float dt,const char* name){
        Updating=true;
        for(auto& [_,entry]:Entries)if(entry.Enabled)try{Invoke(entry.Object,name,scene,dt,true);}catch(const std::exception& error){entry.Enabled=false;std::cerr<<"[Lua System] "<<error.what()<<'\n';}
        Updating=false;
    }
    void OnUpdate(Scene& scene,float dt) override { Run(scene,dt,"OnUpdate"); }
    void OnFixedUpdate(Scene& scene,float dt) override { Run(scene,dt,"OnFixedUpdate"); }
    void OnDestroy(Scene& scene) override {
        for(auto& [_,entry]:Entries)try{Invoke(entry.Object,"OnDestroy",scene,0,false);}catch(const std::exception& error){std::cerr<<"[Lua System] "<<error.what()<<'\n';}
        Entries.clear();
    }
};
}
