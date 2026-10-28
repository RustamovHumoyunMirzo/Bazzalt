#include "Bazzalt/Application.h"
#include "Bazzalt/Subsystem.h"
#include "Runtime/ApplicationAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/SubsystemRuntime.h"
#include "Runtime/Engine.h"
#include "Runtime/LuaRuntime.h"
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdlib>
using namespace Bazzalt;
using Runtime::ApplicationEventsAccess;
namespace {
int Owner;
auto Listen(ApplicationEventType type,ApplicationEvents::Callback callback){return ApplicationEventsAccess::Subscribe(type,std::move(callback),&Owner);}
void Mode(const char* value){
#ifdef _WIN32
    _putenv_s("BAZZALT_SUBSYSTEM_PROBE",value);
#else
    setenv("BAZZALT_SUBSYSTEM_PROBE",value,1);
#endif
}
}
int main(int argc,char** argv){
    assert(argc==2);
    std::vector<int> calls;ApplicationEvents::Subscription second=0,added=0;
    auto first=Listen(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(1);ApplicationEvents::Unsubscribe(second);if(!added)added=Listen(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(3);});});
    second=Listen(ApplicationEventType::OnBoot,[&](const auto&){calls.push_back(2);});
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});assert(calls==std::vector<int>{1});
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});assert((calls==std::vector<int>{1,1,3}));
    ApplicationEvents::Unsubscribe(first);ApplicationEvents::Unsubscribe(added);
    bool denied=false;try{Application::OnLateUpdate([](float){});}catch(const std::logic_error&){denied=true;}assert(denied);
    auto previousOwner=ApplicationEventsAccess::SetOwner(&Owner);auto previousLevel=ApplicationEventsAccess::SetSubsystemLevel(4);
    denied=false;try{Application::OnInit([]{});}catch(const std::logic_error&){denied=true;}assert(denied);
    auto late=Application::OnLateUpdate([&](float dt){assert(dt==.125f);calls.push_back(4);});
    ApplicationEventsAccess::SetSubsystemLevel(previousLevel);ApplicationEventsAccess::SetOwner(previousOwner);
    ApplicationEventsAccess::Emit({ApplicationEventType::OnLateUpdate,.125f});assert(calls.back()==4);
    ApplicationEventsAccess::DisconnectOwner(&Owner);assert(!ApplicationEvents::Unsubscribe(late));
    Application::SetTargetFrameRate(144);assert(Application::GetTargetFrameRate()==144);
    Application::SetTargetFrameRate(0);assert(Application::GetTargetFrameRate()==0);
    denied=false;try{Application::SetTargetFrameRate(-1);}catch(const std::out_of_range&){denied=true;}assert(denied);
    Runtime::ApplicationAccess::Advance(.25);assert(Application::GetFrameRate()==4);
    Runtime::ApplicationAccess::Reset(false,{});Mode("");
    Runtime::SubsystemRuntime subsystems;const Runtime::SubsystemBinding binding{UUID::Generate(),std::filesystem::u8path(argv[1]),0};
    subsystems.Configure({binding});subsystems.ReachLevel(0);assert(Subsystems::Find("SubsystemProbe"));
    ApplicationEventsAccess::Emit({ApplicationEventType::OnBoot});ApplicationEventsAccess::ThrowIfFailed();
    subsystems.Stop();assert(!Subsystems::Find("SubsystemProbe"));
    Mode("lower");auto ready=binding;ready.Level=4;subsystems.Configure({ready});
    denied=false;try{subsystems.ReachLevel(4);}catch(const std::logic_error&){denied=true;}assert(denied);subsystems.Stop();
    Mode("early");subsystems.Configure({binding});
    denied=false;try{subsystems.ReachLevel(0);}catch(const std::logic_error&){denied=true;}assert(denied);subsystems.Stop();Mode("");
    std::vector<ApplicationEventType> order;std::vector<ApplicationEvents::Subscription> subscriptions;
    for(int i=0;i<=int(ApplicationEventType::OnAfterPhysicsShutdown);++i)subscriptions.push_back(Listen(ApplicationEventType(i),[&](const auto& event){order.push_back(event.Type);}));
    {Runtime::Engine engine;assert(engine.Init(true));engine.Update();engine.BeginShutdown();engine.Shutdown(true);engine.EndApplication();}
    assert(order.front()==ApplicationEventType::OnBoot);
    const auto before=[&](auto a,auto b){assert(std::find(order.begin(),order.end(),a)<std::find(order.begin(),order.end(),b));};
    before(ApplicationEventType::OnBeforeInputInit,ApplicationEventType::OnInputInit);before(ApplicationEventType::OnAfterInputInit,ApplicationEventType::OnBeforeAudioInit);
    before(ApplicationEventType::OnAudioInit,ApplicationEventType::OnBeforePhysicsInit);before(ApplicationEventType::OnPhysicsInit,ApplicationEventType::OnLoad);
    before(ApplicationEventType::OnBeforePhysicsShutdown,ApplicationEventType::OnAfterPhysicsShutdown);
    before(ApplicationEventType::OnUpdate,ApplicationEventType::OnLateUpdate);before(ApplicationEventType::OnLateUpdate,ApplicationEventType::OnPreRender);
    assert(std::count(order.begin(),order.end(),ApplicationEventType::OnShutdown)==1);
    for(auto id:subscriptions)ApplicationEvents::Unsubscribe(id);
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-controls-"+UUID::Generate().ToString());std::filesystem::create_directories(folder);
    struct Cleanup{std::filesystem::path Folder;~Cleanup(){std::error_code error;std::filesystem::remove_all(Folder,error);}} cleanup{folder};
    const auto source=folder/"Controls.lua",bytecode=folder/"Controls.blua";
    {std::ofstream file(source);file<<R"lua(
local B=Bazzalt
local W=COMPONENT("Controls")
function W:OnCreate()
    B.Application:SetTargetFrameRate(120)
    assert(B.Application:GetTargetFrameRate()==120)
    B.Application.SetRunInBackground(false)
    assert(not B.Application.GetRunInBackground())
    assert(not pcall(function() B.Application:OnLateUpdate(function() end) end))
    assert(not pcall(function() B.Application.SetTargetFrameRate(-1) end))
    B.Application:Quit(7)
    assert(B.Application:IsQuitting() and B.Application:GetExitCode()==7)
end
return W
)lua";}
    Runtime::Engine engine;auto entity=engine.GetScene().CreateEntity("Controls");std::string error;
    Runtime::LuaRuntime lua;assert(Runtime::LuaRuntime::Import(source,bytecode,error));
    Runtime::ScriptBinding script;script.Module=bytecode;script.Entity=entity.GetUUID().ToString();script.TypeName="Controls";
    assert(lua.Configure({script},error));assert(lua.Start(error));lua.Stop();assert(Application::GetExitCode()==7);
}
