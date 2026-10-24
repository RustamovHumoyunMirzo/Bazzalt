#include "Script/LuaBindings.h"
#include "Runtime/ApplicationEventsAccess.h"
namespace Bazzalt::Runtime {
void BindLuaApplicationEvents(sol::table api){
    auto kinds=api.create_named("ApplicationEventType");
    for(int i=0;i<=int(ApplicationEventType::OnShutdown);++i){auto type=ApplicationEventType(i);kinds[ApplicationEvents::GetName(type)]=type;}
    auto event=api.new_usertype<ApplicationEvent>("ApplicationEvent",sol::no_constructor);
    event["Type"]=&ApplicationEvent::Type;event["DeltaTime"]=&ApplicationEvent::DeltaTime;
    event["Width"]=&ApplicationEvent::Width;event["Height"]=&ApplicationEvent::Height;
    event["IsFocused"]=&ApplicationEvent::IsFocused;event["SceneUUID"]=&ApplicationEvent::SceneUUID;
    event["ScenePath"]=&ApplicationEvent::ScenePath;
    auto events=api.create_named("ApplicationEvents");
    events["Unsubscribe"]=&ApplicationEvents::Unsubscribe;
    events["GetName"]=&ApplicationEvents::GetName;
    events["Subscribe"]=[](ApplicationEventType type,sol::protected_function callback){
        auto* owner=callback.lua_state();
        return ApplicationEventsAccess::Subscribe(type,[callback](const ApplicationEvent& value) mutable {
            lua_sethook(callback.lua_state(),[](lua_State* state,lua_Debug*){luaL_error(state,"Application callback instruction budget exceeded");},LUA_MASKCOUNT,1000000);
            auto result=callback(value);lua_sethook(callback.lua_state(),nullptr,0,0);
            if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
        },owner);
    };
}
}
