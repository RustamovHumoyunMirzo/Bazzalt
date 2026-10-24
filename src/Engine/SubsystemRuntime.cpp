#include "Runtime/SubsystemRuntime.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Bazzalt/Console.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
namespace Bazzalt { namespace { std::unordered_map<std::string,Subsystem*> Loaded; }
Subsystem* Subsystems::Find(const std::string& name){auto found=Loaded.find(name);return found==Loaded.end()?nullptr:found->second;}
}
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
struct Scope {
    std::shared_ptr<void> Module;const void* Owner;int Level;
    Scope(std::shared_ptr<void> module,const void* owner,int level):Module(Detail::SetCurrentGameplayModule(std::move(module))),Owner(ApplicationEventsAccess::SetOwner(owner)),Level(ApplicationEventsAccess::SetSubsystemLevel(level)){}
    ~Scope(){Detail::SetCurrentGameplayModule(std::move(Module));ApplicationEventsAccess::SetOwner(Owner);ApplicationEventsAccess::SetSubsystemLevel(Level);}
};
}
struct SubsystemRuntime::Instance {
    std::shared_ptr<void> Library;
    const SubsystemModuleApi* Api=nullptr;void* Object=nullptr;int Level=0;bool Published=false;
    ~Instance(){if(!Api||!Object)return;if(Published)Loaded.erase(Api->TypeName);Scope scope(Library,Object,Level);try{Api->Unregister(Object);}catch(const std::exception& error){ConsoleError(error.what());}catch(...){ConsoleError("Subsystem OnUnregister failed");}ApplicationEventsAccess::DisconnectOwner(Object);try{Api->Destroy(Object);}catch(...){} }
    static void ConsoleError(const char* text);
};
SubsystemRuntime::SubsystemRuntime()=default;
void SubsystemRuntime::Instance::ConsoleError(const char* text){ApplicationEventsAccess::Fail(text);}
SubsystemRuntime::~SubsystemRuntime(){Stop();}
void SubsystemRuntime::Configure(std::vector<SubsystemBinding> bindings){Stop();m_bindings=std::move(bindings);std::stable_sort(m_bindings.begin(),m_bindings.end(),[](const auto& a,const auto& b){return a.Level<b.Level;});}
void SubsystemRuntime::ReachLevel(int level){
    if(level<=m_level)return;
    for(const auto& binding:m_bindings)if(binding.Level>m_level&&binding.Level<=level){
        auto instance=std::make_unique<Instance>();instance->Level=binding.Level;
#ifdef _WIN32
        auto library=LoadLibraryW(binding.Module.c_str());if(library)instance->Library={library,[](void* value){FreeLibrary(static_cast<HMODULE>(value));}};
        auto entry=library?reinterpret_cast<GetSubsystemModuleApi>(GetProcAddress(library,"BazzaltGetSubsystemModuleV1")):nullptr;
#else
        auto library=dlopen(binding.Module.c_str(),RTLD_NOW|RTLD_LOCAL);if(library)instance->Library={library,[](void* value){dlclose(value);}};
        auto entry=library?reinterpret_cast<GetSubsystemModuleApi>(dlsym(library,"BazzaltGetSubsystemModuleV1")):nullptr;
#endif
        if(!entry)throw std::runtime_error("Subsystem module is missing or was compiled as an entity script: "+binding.Asset.ToString());
        instance->Api=entry();auto* api=instance->Api;
        if(!api||api->AbiVersion!=SubsystemAbiVersion||!api->TypeName||!api->Create||!api->Destroy||!api->Register||!api->Unregister)throw std::runtime_error("Invalid subsystem module ABI");
        Scope scope(instance->Library,nullptr,binding.Level);instance->Object=api->Create();if(!instance->Object)throw std::runtime_error("Subsystem construction failed");
        if(!Loaded.emplace(api->TypeName,static_cast<Subsystem*>(instance->Object)).second)throw std::runtime_error("Duplicate loaded subsystem name");
        instance->Published=true;
        Scope registered(instance->Library,instance->Object,binding.Level);api->Register(instance->Object);m_instances.push_back(std::move(instance));
    }
    m_level=level;
}
void SubsystemRuntime::Stop() noexcept{while(!m_instances.empty())m_instances.pop_back();m_level=-1;}
}
