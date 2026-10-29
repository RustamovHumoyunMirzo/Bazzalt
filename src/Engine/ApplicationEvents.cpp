#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ApplicationAccess.h"
#include "Runtime/GlobalEventsAccess.h"
#include "Runtime/GraphicsAccess.h"
#include "Physics/PhysicsModuleApi.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include <map>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string_view>
#include <iterator>
#include <optional>
namespace Bazzalt {
namespace {
struct Entry {
    // Lease must outlive the callback's destructor.
    std::shared_ptr<void> Module;
    ApplicationEvents::Callback Callback;
    ApplicationEventType Type;
    const void* Owner;
    int Level=-1;
    bool AssetAccess=false;
};
struct Registry { std::map<std::uint64_t,std::shared_ptr<Entry>> Entries;std::uint64_t Next=1; };
Registry& State(){static Registry state;return state;}
thread_local const void* CurrentOwner=nullptr;
thread_local int CurrentLevel=-1;
thread_local bool CurrentAssetAccess=false;
std::function<void(int)> StageLoader;
std::string Failure;
bool WindowReady=false;
std::optional<ApplicationEvent> PendingFocus,PendingResize,PendingWindow;
std::vector<ApplicationEvent> PendingContent;
}
const char* ApplicationEvents::GetName(ApplicationEventType type){
    static constexpr const char* names[]={"OnBoot","OnBeforeWindowCreate","OnInit","OnLoad","OnPostWindowCreate","OnPreUpdate","OnUpdate","OnFixedUpdate","OnLateUpdate","OnPreRender","OnRender","OnPostRender","OnWindowResize","OnWindowFocusChanged","OnWindowCloseRequested","OnSceneLoad","OnSceneLoaded","OnSceneUnload","OnSceneUnloaded","OnPlay","OnStop","OnPreShutdown","OnShutdown","OnBeforeInputInit","OnInputInit","OnAfterInputInit","OnBeforeAudioInit","OnAudioInit","OnAfterAudioInit","OnBeforePhysicsInit","OnPhysicsInit","OnAfterPhysicsInit","OnBeforePhysicsStep","OnAfterPhysicsStep","OnBeforePhysicsShutdown","OnAfterPhysicsShutdown"};
    auto index=static_cast<unsigned>(type);return index<std::size(names)?names[index]:"Unknown";
}
ApplicationEvents::Subscription ApplicationEvents::Subscribe(ApplicationEventType type,Callback callback){
    if(CurrentLevel<0||!CurrentOwner)throw std::logic_error("Global application events may only be registered inside C++ Subsystem Script callbacks");
    if(Runtime::ApplicationEventsAccess::GetLevelOrder(Runtime::ApplicationEventsAccess::GetEventLevel(type))<Runtime::ApplicationEventsAccess::GetLevelOrder(CurrentLevel))throw std::logic_error(std::string("Cannot subscribe to lower-level event ")+GetName(type));
    return Runtime::ApplicationEventsAccess::Subscribe(type,std::move(callback),CurrentOwner);
}
bool ApplicationEvents::Unsubscribe(Subscription id){auto found=State().Entries.find(id);if(found==State().Entries.end())return false;auto retained=found->second;State().Entries.erase(found);return true;}
const void* Runtime::ApplicationEventsAccess::SetOwner(const void* owner) noexcept {auto previous=CurrentOwner;CurrentOwner=owner;return previous;}
int Runtime::ApplicationEventsAccess::SetSubsystemLevel(int level) noexcept{const auto previous=CurrentLevel;CurrentLevel=level;return previous;}
int Runtime::ApplicationEventsAccess::GetSubsystemLevel() noexcept{return CurrentLevel;}
bool Runtime::ApplicationEventsAccess::SetAssetAccess(bool enabled) noexcept{const bool old=CurrentAssetAccess;CurrentAssetAccess=enabled;return old;}
bool Runtime::ApplicationEventsAccess::GetAssetAccess() noexcept{return CurrentAssetAccess;}
int Runtime::ApplicationEventsAccess::GetLevelOrder(int level) noexcept {
    constexpr int order[]={0,10,20,60,70,21,22,23,30,31,32,40,41,42};
    return level>=0&&level<int(std::size(order))?order[level]:-1;
}
int Runtime::ApplicationEventsAccess::GetEventLevel(ApplicationEventType type) noexcept{
    switch(type){
    case ApplicationEventType::OnBeforeInputInit:return 5;case ApplicationEventType::OnInputInit:return 6;case ApplicationEventType::OnAfterInputInit:return 7;
    case ApplicationEventType::OnBeforeAudioInit:return 8;case ApplicationEventType::OnAudioInit:return 9;case ApplicationEventType::OnAfterAudioInit:return 10;
    case ApplicationEventType::OnBeforePhysicsInit:return 11;case ApplicationEventType::OnPhysicsInit:return 12;case ApplicationEventType::OnAfterPhysicsInit:return 13;
    case ApplicationEventType::OnBoot:return 0;case ApplicationEventType::OnBeforeWindowCreate:return 1;case ApplicationEventType::OnInit:return 2;case ApplicationEventType::OnLoad:case ApplicationEventType::OnSceneLoad:return 3;default:return 4;}
}
void Runtime::ApplicationEventsAccess::BindStageLoader(std::function<void(int)> loader){if(loader)Failure.clear();WindowReady=false;PendingFocus.reset();PendingResize.reset();PendingWindow.reset();PendingContent.clear();StageLoader=std::move(loader);}
void Runtime::ApplicationEventsAccess::CompleteInitialization(const std::string& sceneUUID,bool editor){
    auto content=std::move(PendingContent);PendingContent.clear();
    if(content.empty()){Emit({ApplicationEventType::OnLoad});ApplicationEvent loaded{ApplicationEventType::OnSceneLoaded};loaded.SceneUUID=sceneUUID;Emit(loaded);}
    else for(auto& event:content)Emit(std::move(event));
    auto window=std::move(PendingWindow);PendingWindow.reset();if(window)Emit(*window);else if(editor)Emit({ApplicationEventType::OnPostWindowCreate});
    if(StageLoader)StageLoader(4);ThrowIfFailed();
}
void Runtime::ApplicationEventsAccess::ThrowIfFailed(){if(!Failure.empty()){auto error=std::move(Failure);Failure.clear();throw std::runtime_error(error);}}
void Runtime::ApplicationEventsAccess::Fail(const std::string& error) noexcept{if(Failure.empty())Failure=error;Application::Quit(1);Console::Error(error,"Subsystem");std::cerr<<"[Subsystem] "<<error<<'\n';}
ApplicationEvents::Subscription Runtime::ApplicationEventsAccess::Subscribe(ApplicationEventType type,ApplicationEvents::Callback callback,const void* owner){
    if(!callback||std::string_view(ApplicationEvents::GetName(type))=="Unknown")throw std::invalid_argument("Invalid application event subscription");
    auto& state=State();const auto id=state.Next++;
    state.Entries.emplace(id,std::make_shared<Entry>(Entry{Detail::GetCurrentGameplayModule(),std::move(callback),type,owner,CurrentLevel,CurrentAssetAccess}));return id;
}
void Runtime::ApplicationEventsAccess::DisconnectOwner(const void* owner) noexcept {
    DisconnectPhysicsOwner(owner);
    GlobalEventsAccess::DisconnectOwner(owner);
    GraphicsAccess::DisconnectScope(owner);
    // Destroy callbacks outside map iteration: captures may unsubscribe others.
    std::vector<ApplicationEvents::Subscription> ids;for(auto& [id,entry]:State().Entries)if(entry->Owner==owner)ids.push_back(id);
    for(auto id:ids)ApplicationEvents::Unsubscribe(id);
}
void Runtime::ApplicationEventsAccess::Emit(ApplicationEvent event) noexcept {
    if(StageLoader&&!ApplicationAccess::AreRuntimeServicesReady()){
        if(event.Type==ApplicationEventType::OnLoad||event.Type==ApplicationEventType::OnSceneLoad||event.Type==ApplicationEventType::OnSceneLoaded){if(PendingContent.size()>=128){Fail("Deferred content lifecycle exceeded limit");return;}PendingContent.push_back(std::move(event));return;}
        if(event.Type==ApplicationEventType::OnPostWindowCreate){WindowReady=true;PendingWindow=event;return;}
    }
    ApplicationAccess::Observe(event);
    const auto failed=[](const std::string& error){Fail(error);};
    if(StageLoader&&event.Type==ApplicationEventType::OnWindowFocusChanged&&!Application::IsPlaying()){PendingFocus=event;return;}
    if(StageLoader&&!WindowReady){
        if(event.Type==ApplicationEventType::OnWindowFocusChanged){PendingFocus=event;return;}
        if(event.Type==ApplicationEventType::OnWindowResize){PendingResize=event;return;}
    }
    if(event.Type==ApplicationEventType::OnPostWindowCreate)WindowReady=true;
    if(StageLoader&&(event.Type==ApplicationEventType::OnBoot||event.Type==ApplicationEventType::OnBeforeWindowCreate||event.Type==ApplicationEventType::OnInit||event.Type==ApplicationEventType::OnLoad||event.Type==ApplicationEventType::OnPostWindowCreate||(event.Type==ApplicationEventType::OnBeforeInputInit||event.Type==ApplicationEventType::OnInputInit||event.Type==ApplicationEventType::OnAfterInputInit||event.Type==ApplicationEventType::OnBeforeAudioInit||event.Type==ApplicationEventType::OnAudioInit||event.Type==ApplicationEventType::OnAfterAudioInit||event.Type==ApplicationEventType::OnBeforePhysicsInit||event.Type==ApplicationEventType::OnPhysicsInit||event.Type==ApplicationEventType::OnAfterPhysicsInit))){
        try{StageLoader(GetEventLevel(event.Type));}catch(const std::exception& error){failed(error.what());return;}catch(...){failed("Subsystem loading failed");return;}
    }
    // Snapshot IDs: removed callbacks are skipped; new callbacks wait until next emission.
    std::vector<ApplicationEvents::Subscription> ids;for(auto& [id,entry]:State().Entries)if(entry->Type==event.Type)ids.push_back(id);
    for(auto id:ids){auto found=State().Entries.find(id);if(found==State().Entries.end())continue;auto entry=found->second;
        auto previous=Detail::SetCurrentGameplayModule(entry->Module);
        auto owner=SetOwner(entry->Owner);
        const int level=SetSubsystemLevel(entry->Level);
        const bool access=SetAssetAccess(entry->AssetAccess);
        try{entry->Callback(event);}catch(const std::exception& error){failed(std::string(ApplicationEvents::GetName(event.Type))+": "+error.what());}catch(...){failed("Subsystem callback failed");}
        Detail::SetCurrentGameplayModule(std::move(previous));
        SetOwner(owner);
        SetSubsystemLevel(level);SetAssetAccess(access);
    }
    if(event.Type==ApplicationEventType::OnPostWindowCreate){
        auto resize=std::move(PendingResize);PendingResize.reset();if(resize)Emit(*resize);
    }
    if(event.Type==ApplicationEventType::OnPlay){auto focus=std::move(PendingFocus);PendingFocus.reset();if(focus)Emit(*focus);}
}
}
