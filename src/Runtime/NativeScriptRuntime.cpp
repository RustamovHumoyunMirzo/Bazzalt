#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Script.h"
#include "Runtime/TimeAccess.h"
#include <exception>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
struct NativeScriptRuntime::Instance {
#ifdef _WIN32
    HMODULE Library{};
#else
    void* Library{};
#endif
    const ScriptModuleApi* Api{};void* Object{};
    void (*OnFixedUpdate)(void*,float) = nullptr;
};
NativeScriptRuntime::NativeScriptRuntime()=default;
NativeScriptRuntime::~NativeScriptRuntime(){Stop();}
bool NativeScriptRuntime::Configure(std::vector<ScriptBinding> bindings,std::string& error){if(!m_instances.empty()){error="Cannot reconfigure scripts while Play mode is running";return false;}m_bindings=std::move(bindings);error.clear();return true;}
bool NativeScriptRuntime::Start(std::string& error){
    Stop();error.clear();
    for(const auto& binding:m_bindings){Instance instance;
#ifdef _WIN32
        instance.Library=LoadLibraryW(binding.Module.c_str());auto entry=instance.Library?reinterpret_cast<GetScriptModuleApi>(GetProcAddress(instance.Library,"BazzaltGetScriptModuleV1")):nullptr;
#else
        instance.Library=dlopen(binding.Module.c_str(),RTLD_NOW|RTLD_LOCAL);auto entry=instance.Library?reinterpret_cast<GetScriptModuleApi>(dlsym(instance.Library,"BazzaltGetScriptModuleV1")):nullptr;
#endif
        if(!entry){error="Could not load script module: "+binding.Module.string();Stop();return false;}
        instance.Api=entry();
        if(!instance.Api||instance.Api->AbiVersion!=ScriptAbiVersion||!instance.Api->TypeName||binding.TypeName!=instance.Api->TypeName){error="Incompatible script module: "+binding.Module.string();m_instances.push_back(instance);Stop();return false;}
        // Optional service binding preserves compatibility with existing V1 modules.
        using BindTime = void (*)(Detail::TimeState*);
#ifdef _WIN32
        auto bindTime=reinterpret_cast<BindTime>(GetProcAddress(instance.Library,"BazzaltBindTimeV1"));
#else
        auto bindTime=reinterpret_cast<BindTime>(dlsym(instance.Library,"BazzaltBindTimeV1"));
#endif
        if(bindTime)bindTime(TimeAccess::GetState());
#ifdef _WIN32
        instance.OnFixedUpdate=reinterpret_cast<decltype(instance.OnFixedUpdate)>(GetProcAddress(instance.Library,"BazzaltFixedUpdateV1"));
#else
        instance.OnFixedUpdate=reinterpret_cast<decltype(instance.OnFixedUpdate)>(dlsym(instance.Library,"BazzaltFixedUpdateV1"));
#endif
        try{instance.Object=instance.Api->Create(binding.Entity.c_str());}catch(...){instance.Object=nullptr;}
        if(!instance.Object){error="Could not create script component: "+binding.TypeName;m_instances.push_back(instance);Stop();return false;}
        for(const auto& [name,value]:binding.Properties)if(instance.Api->SetProperty&&!instance.Api->SetProperty(instance.Object,name.c_str(),value.c_str())){error="Invalid property '"+name+"' on "+binding.TypeName;m_instances.push_back(instance);Stop();return false;}
        m_instances.push_back(instance);
    }
    try{for(auto& instance:m_instances)instance.Api->OnCreate(instance.Object);}catch(const std::exception& e){error=std::string("Script OnCreate failed: ")+e.what();Stop();return false;}catch(...){error="Script OnCreate failed";Stop();return false;}return true;
}
void NativeScriptRuntime::Update(float deltaTime){for(auto& instance:m_instances)try{instance.Api->OnUpdate(instance.Object,deltaTime);}catch(...) {}}
void NativeScriptRuntime::FixedUpdate(float deltaTime){for(auto& instance:m_instances)if(instance.OnFixedUpdate)try{instance.OnFixedUpdate(instance.Object,deltaTime);}catch(...) {}}
void NativeScriptRuntime::Stop() noexcept{for(auto it=m_instances.rbegin();it!=m_instances.rend();++it)if(it->Object&&it->Api){try{it->Api->OnDestroy(it->Object);}catch(...){}try{it->Api->Destroy(it->Object);}catch(...){}}for(auto it=m_instances.rbegin();it!=m_instances.rend();++it)if(it->Library){
#ifdef _WIN32
FreeLibrary(it->Library);
#else
dlclose(it->Library);
#endif
}m_instances.clear();}
}
