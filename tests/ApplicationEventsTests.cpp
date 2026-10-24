#include "Bazzalt/Application.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/Engine.h"
#include "Runtime/LuaRuntime.h"
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace Bazzalt;
using Runtime::ApplicationEventsAccess;
int main(){
    std::vector<int> calls;ApplicationEvents::Subscription second=0,added=0;
    auto first=ApplicationEvents::Subscribe(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(1);ApplicationEvents::Unsubscribe(second);if(!added)added=ApplicationEvents::Subscribe(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(3);});});
    second=ApplicationEvents::Subscribe(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(2);});
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});assert(calls==std::vector<int>{1});
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});assert((calls==std::vector<int>{1,1,3}));
    ApplicationEvents::Unsubscribe(first);ApplicationEvents::Unsubscribe(added);
    auto bad=ApplicationEvents::Subscribe(ApplicationEventType::OnBoot,[](const auto&){throw std::runtime_error("expected test failure");});
    auto good=ApplicationEvents::Subscribe(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(4);});
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});assert(calls.back()==4);
    ApplicationEvents::Unsubscribe(bad);ApplicationEvents::Unsubscribe(good);
    int owner;auto owned=ApplicationEventsAccess::Subscribe(ApplicationEventType::OnBoot,[](const auto&){assert(false);},&owner);
    ApplicationEventsAccess::DisconnectOwner(&owner);assert(!ApplicationEvents::Unsubscribe(owned));
    struct App:Application { unsigned Width=0;bool Focus=false;void OnWindowResize(std::uint32_t width,std::uint32_t) override{Width=width;}void OnWindowFocusChanged(bool focused) override{Focus=focused;} } app;
    ApplicationEvent resized{ApplicationEventType::OnWindowResize};resized.Width=512;app.OnApplicationEvent(resized);assert(app.Width==512);
    ApplicationEvent focused{ApplicationEventType::OnWindowFocusChanged};focused.IsFocused=true;app.OnApplicationEvent(focused);assert(app.Focus);

    std::vector<ApplicationEventType> order;std::vector<ApplicationEvents::Subscription> subscriptions;
    for(int i=0;i<=int(ApplicationEventType::OnShutdown);++i)subscriptions.push_back(ApplicationEvents::Subscribe(ApplicationEventType(i),[&](const auto& event){order.push_back(event.Type);}));
    {
        Runtime::Engine engine;assert(engine.Init(true));engine.Update();engine.BeginShutdown();engine.Shutdown(true);engine.EndApplication();
    }
    assert(order.front()==ApplicationEventType::OnBoot);
    const auto before=[&](auto a,auto b){assert(std::find(order.begin(),order.end(),a)<std::find(order.begin(),order.end(),b));};
    before(ApplicationEventType::OnBoot,ApplicationEventType::OnInit);before(ApplicationEventType::OnPreUpdate,ApplicationEventType::OnUpdate);
    before(ApplicationEventType::OnUpdate,ApplicationEventType::OnLateUpdate);before(ApplicationEventType::OnLateUpdate,ApplicationEventType::OnPreRender);
    before(ApplicationEventType::OnPreRender,ApplicationEventType::OnRender);before(ApplicationEventType::OnRender,ApplicationEventType::OnPostRender);
    before(ApplicationEventType::OnPreShutdown,ApplicationEventType::OnStop);before(ApplicationEventType::OnSceneUnload,ApplicationEventType::OnSceneUnloaded);
    assert(std::count(order.begin(),order.end(),ApplicationEventType::OnShutdown)==1);
    for(auto id:subscriptions)ApplicationEvents::Unsubscribe(id);

    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-events-"+UUID::Generate().ToString());std::filesystem::create_directories(folder);
    struct Cleanup{std::filesystem::path Folder;~Cleanup(){std::error_code error;std::filesystem::remove_all(Folder,error);}} cleanup{folder};
    const auto source=folder/"App.lua",bytecode=folder/"App.blua";
    {std::ofstream file(source);file<<R"lua(
local B=Bazzalt
local App={}
function App:OnBoot() assert(os==nil and io==nil);self.Booted=true end
function App:OnInit() assert(self.Booted) end
function App:OnWindowResize(width,height) assert(width==512 and height==0);B.Time.SetTimeScale(.25) end
function App:OnLateUpdate(dt) assert(dt==.125);self.Late=true end
function App:OnShutdown() assert(self.Late);B.Time.SetTimeScale(1) end
B.ApplicationEvents.Subscribe(B.ApplicationEventType.OnPlay,function(event) assert(event.Type==B.ApplicationEventType.OnPlay);B.Time.SetTimeScale(.5) end)
return App
)lua";}
    std::string error;
    {Runtime::LuaRuntime lua;assert(Runtime::LuaRuntime::Import(source,bytecode,error));assert(lua.StartApplication(bytecode,error));
     lua.ApplicationEvent({ApplicationEventType::OnBoot});lua.ApplicationEvent({ApplicationEventType::OnInit});lua.ApplicationEvent(resized);assert(Time::GetTimeScale()==.25f);
     ApplicationEventsAccess::Emit({ApplicationEventType::OnPlay});assert(Time::GetTimeScale()==.5f);
     lua.ApplicationEvent({ApplicationEventType::OnLateUpdate,.125f});lua.ApplicationEvent({ApplicationEventType::OnShutdown});}
    ApplicationEventsAccess::Emit({ApplicationEventType::OnPlay});assert(Time::GetTimeScale()==1);
}
