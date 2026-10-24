#include "Script/LuaBindings.h"
#include "Bazzalt/ApplicationControl.h"
namespace Bazzalt::Runtime {
void BindLuaApplicationEvents(sol::table api){
    auto app=api.create_named("Application");
#define APP_GET(Name) app[#Name]=sol::overload(&Application::Name,[](sol::table){return Application::Name();});
    APP_GET(GetTargetFrameRate) APP_GET(GetBackgroundFrameRate) APP_GET(GetRunInBackground)
    APP_GET(GetFrameRate) APP_GET(GetFrameCount) APP_GET(IsFocused) APP_GET(IsPlaying)
    APP_GET(IsEditor) APP_GET(IsQuitting) APP_GET(GetExitCode) APP_GET(GetWindowWidth)
    APP_GET(GetWindowHeight) APP_GET(GetName) APP_GET(GetCompanyName) APP_GET(GetVersion) APP_GET(GetPlatform)
#undef APP_GET
    app["SetTargetFrameRate"]=sol::overload(&Application::SetTargetFrameRate,[](sol::table,int value){Application::SetTargetFrameRate(value);});
    app["SetBackgroundFrameRate"]=sol::overload(&Application::SetBackgroundFrameRate,[](sol::table,int value){Application::SetBackgroundFrameRate(value);});
    app["SetRunInBackground"]=sol::overload(&Application::SetRunInBackground,[](sol::table,bool value){Application::SetRunInBackground(value);});
    app["Quit"]=sol::overload([](sol::optional<int> code){Application::Quit(code.value_or(0));},[](sol::table,sol::optional<int> code){Application::Quit(code.value_or(0));});
    auto denied=[](sol::variadic_args){throw std::logic_error("Application event listeners require a registered C++ Subsystem Script");};
    for(int i=0;i<=int(ApplicationEventType::OnShutdown);++i)app[ApplicationEvents::GetName(ApplicationEventType(i))]=denied;
    auto legacy=api.create_named("ApplicationEvents");legacy["Subscribe"]=denied;legacy["Unsubscribe"]=denied;
}
}
