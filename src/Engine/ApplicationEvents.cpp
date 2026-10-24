#include "Runtime/ApplicationEventsAccess.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include <map>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string_view>
#include <iterator>
namespace Bazzalt {
namespace {
struct Entry {
    // Lease must outlive the callback's destructor.
    std::shared_ptr<void> Module;
    ApplicationEvents::Callback Callback;
    ApplicationEventType Type;
    const void* Owner;
};
struct Registry { std::map<std::uint64_t,std::shared_ptr<Entry>> Entries;std::uint64_t Next=1; };
Registry& State(){static Registry state;return state;}
thread_local const void* CurrentOwner=nullptr;
}
const char* ApplicationEvents::GetName(ApplicationEventType type){
    static constexpr const char* names[]={"OnBoot","OnBeforeWindowCreate","OnInit","OnLoad","OnPostWindowCreate","OnPreUpdate","OnUpdate","OnFixedUpdate","OnLateUpdate","OnPreRender","OnRender","OnPostRender","OnWindowResize","OnWindowFocusChanged","OnWindowCloseRequested","OnSceneLoad","OnSceneLoaded","OnSceneUnload","OnSceneUnloaded","OnPlay","OnStop","OnPreShutdown","OnShutdown"};
    auto index=static_cast<unsigned>(type);return index<std::size(names)?names[index]:"Unknown";
}
ApplicationEvents::Subscription ApplicationEvents::Subscribe(ApplicationEventType type,Callback callback){return Runtime::ApplicationEventsAccess::Subscribe(type,std::move(callback),CurrentOwner);}
bool ApplicationEvents::Unsubscribe(Subscription id){auto found=State().Entries.find(id);if(found==State().Entries.end())return false;auto retained=found->second;State().Entries.erase(found);return true;}
const void* Runtime::ApplicationEventsAccess::SetOwner(const void* owner) noexcept {auto previous=CurrentOwner;CurrentOwner=owner;return previous;}
ApplicationEvents::Subscription Runtime::ApplicationEventsAccess::Subscribe(ApplicationEventType type,ApplicationEvents::Callback callback,const void* owner){
    if(!callback||std::string_view(ApplicationEvents::GetName(type))=="Unknown")throw std::invalid_argument("Invalid application event subscription");
    auto& state=State();const auto id=state.Next++;
    state.Entries.emplace(id,std::make_shared<Entry>(Entry{Detail::GetCurrentGameplayModule(),std::move(callback),type,owner}));return id;
}
void Runtime::ApplicationEventsAccess::DisconnectOwner(const void* owner) noexcept {
    // Destroy callbacks outside map iteration: captures may unsubscribe others.
    std::vector<ApplicationEvents::Subscription> ids;for(auto& [id,entry]:State().Entries)if(entry->Owner==owner)ids.push_back(id);
    for(auto id:ids)ApplicationEvents::Unsubscribe(id);
}
void Runtime::ApplicationEventsAccess::Emit(ApplicationEvent event) noexcept {
    // Snapshot IDs: removed callbacks are skipped; new callbacks wait until next emission.
    std::vector<ApplicationEvents::Subscription> ids;for(auto& [id,entry]:State().Entries)if(entry->Type==event.Type)ids.push_back(id);
    for(auto id:ids){auto found=State().Entries.find(id);if(found==State().Entries.end())continue;auto entry=found->second;
        auto previous=Detail::SetCurrentGameplayModule(entry->Module);
        auto owner=SetOwner(entry->Owner);
        try{entry->Callback(event);}catch(const std::exception& error){std::cerr<<"[Application] "<<ApplicationEvents::GetName(event.Type)<<": "<<error.what()<<'\n';}catch(...){std::cerr<<"[Application] callback failed\n";}
        Detail::SetCurrentGameplayModule(std::move(previous));
        SetOwner(owner);
    }
}
}
