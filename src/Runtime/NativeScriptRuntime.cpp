#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/GraphicsAccess.h"
#include "Runtime/ExtensionAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/BehaviorAccess.h"
#include "Bazzalt/Script.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include "Bazzalt/EntityReference.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Scene.h"
#include "Runtime/TimeAccess.h"
#include "Runtime/InputAccess.h"
#include "Runtime/MaterialLibrary.h"
#include <exception>
#include <map>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace Bazzalt::Runtime {
namespace {
struct ModuleScope {
    std::shared_ptr<void> Previous;
    const void* PreviousOwner;
    const void* PreviousGraphicsOwner;
    std::string PreviousExtension;
    explicit ModuleScope(std::shared_ptr<void> module,const void* owner=nullptr,const std::string& type="") : Previous(Detail::SetCurrentGameplayModule(std::move(module))),PreviousOwner(ApplicationEventsAccess::SetOwner(owner)),PreviousGraphicsOwner(GraphicsAccess::SetScope(owner)),PreviousExtension(ExtensionAccess::SetCaller(ExtensionAccess::ComponentOwner(type))) {}
    ~ModuleScope() { Detail::SetCurrentGameplayModule(std::move(Previous));ApplicationEventsAccess::SetOwner(PreviousOwner);GraphicsAccess::SetScope(PreviousGraphicsOwner);ExtensionAccess::SetCaller(std::move(PreviousExtension)); }
};
Detail::EntityServices EntityHostServices{
    [](UUID id, Transform* value) {
        auto* scene = SceneManager::GetActiveScene();
        if (!scene || !value) return false;
        auto entity = scene->GetEntity(id);
        if (!entity) return false;
        *value = entity.GetWorldTransform();
        return true;
    },
    [](UUID id, const Transform* value) {
        auto* scene = SceneManager::GetActiveScene();
        if (!scene || !value) return false;
        auto entity = scene->GetEntity(id);
        if (!entity) return false;
        return entity.SetWorldTransform(*value);
    }
};
}
struct NativeScriptRuntime::Instance {
    std::shared_ptr<void> Lease;
#ifdef _WIN32
    HMODULE Library{};
#else
    void* Library{};
#endif
    const ScriptModuleApi* Api{};void* Object{};
    void (*OnFixedUpdate)(void*,float) = nullptr;
    void (*OnLateUpdate)(void*,float) = nullptr;
    void (*OnApplicationEvent)(void*,const Bazzalt::ApplicationEvent*) = nullptr;
    bool Application=false;
    Behavior* BehaviorObject=nullptr;
};
NativeScriptRuntime::NativeScriptRuntime()=default;
NativeScriptRuntime::~NativeScriptRuntime(){Stop();}
bool NativeScriptRuntime::Configure(std::vector<ScriptBinding> bindings,std::string& error){
    if(!m_instances.empty()){error="Cannot reconfigure scripts while Play mode is running";return false;}
    std::set<std::pair<std::string,std::string>> attached;
    std::map<std::string,std::filesystem::path> types;
    for(const auto& binding:bindings){
        if(!attached.emplace(binding.Entity,binding.TypeName).second){error="Duplicate script component on entity: "+binding.TypeName;return false;}
        const auto module=binding.Module.lexically_normal();
        const auto found=types.find(binding.TypeName);
        if(found!=types.end()&&found->second!=module){error="Ambiguous script component type: "+binding.TypeName;return false;}
        types.emplace(binding.TypeName,module);
    }
    m_bindings=std::move(bindings);error.clear();return true;
}
bool NativeScriptRuntime::Start(std::string& error,bool application){
    Stop();error.clear();
    for(const auto& binding:m_bindings){Instance instance;instance.Application=application;
#ifdef _WIN32
        instance.Library=LoadLibraryExW(binding.Module.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);auto entry=instance.Library?reinterpret_cast<GetScriptModuleApi>(GetProcAddress(instance.Library,"BazzaltGetScriptModuleV1")):nullptr;
#else
        instance.Library=dlopen(binding.Module.c_str(),RTLD_NOW|RTLD_LOCAL);auto entry=instance.Library?reinterpret_cast<GetScriptModuleApi>(dlsym(instance.Library,"BazzaltGetScriptModuleV1")):nullptr;
#endif
        if(instance.Library) instance.Lease=std::shared_ptr<void>(instance.Library,[](void* library){
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(library));
#else
            dlclose(library);
#endif
        });
        if(!entry){error="Could not load script module: "+binding.Module.string();Stop();return false;}
        ModuleScope moduleScope(instance.Lease,nullptr,binding.TypeName);
        instance.Api=entry();
        if(!instance.Api||instance.Api->AbiVersion!=ScriptAbiVersion||!instance.Api->TypeName||(!application&&binding.TypeName!=instance.Api->TypeName)){error="Incompatible script module: "+binding.Module.string();m_instances.push_back(instance);Stop();return false;}
        // Optional service binding preserves compatibility with existing V1 modules.
        using BindTime = void (*)(Detail::TimeState*);
#ifdef _WIN32
        auto bindTime=reinterpret_cast<BindTime>(GetProcAddress(instance.Library,"BazzaltBindTimeV1"));
#else
        auto bindTime=reinterpret_cast<BindTime>(dlsym(instance.Library,"BazzaltBindTimeV1"));
#endif
        if(bindTime)bindTime(TimeAccess::GetState());
        using BindInput = void (*)(Detail::InputState*);
#ifdef _WIN32
        auto bindInput=reinterpret_cast<BindInput>(GetProcAddress(instance.Library,"BazzaltBindInputV1"));
#else
        auto bindInput=reinterpret_cast<BindInput>(dlsym(instance.Library,"BazzaltBindInputV1"));
#endif
        if(bindInput)bindInput(InputAccess::GetState());
        using BindEntities = void (*)(Detail::EntityServices*);
#ifdef _WIN32
        auto bindEntities=reinterpret_cast<BindEntities>(GetProcAddress(instance.Library,"BazzaltBindEntitiesV1"));
#else
        auto bindEntities=reinterpret_cast<BindEntities>(dlsym(instance.Library,"BazzaltBindEntitiesV1"));
#endif
        if(bindEntities)bindEntities(&EntityHostServices);
        using BindMaterials = void (*)(Detail::MaterialServices*);
#ifdef _WIN32
        auto bindMaterials=reinterpret_cast<BindMaterials>(GetProcAddress(instance.Library,"BazzaltBindMaterialsV1"));
#else
        auto bindMaterials=reinterpret_cast<BindMaterials>(dlsym(instance.Library,"BazzaltBindMaterialsV1"));
#endif
        if(bindMaterials)bindMaterials(GetMaterialServices());
#ifdef _WIN32
        instance.OnFixedUpdate=reinterpret_cast<decltype(instance.OnFixedUpdate)>(GetProcAddress(instance.Library,"BazzaltFixedUpdateV1"));
        instance.OnLateUpdate=reinterpret_cast<decltype(instance.OnLateUpdate)>(GetProcAddress(instance.Library,"BazzaltLateUpdateV1"));
        instance.OnApplicationEvent=reinterpret_cast<decltype(instance.OnApplicationEvent)>(GetProcAddress(instance.Library,"BazzaltApplicationEventV1"));
#else
        instance.OnFixedUpdate=reinterpret_cast<decltype(instance.OnFixedUpdate)>(dlsym(instance.Library,"BazzaltFixedUpdateV1"));
        instance.OnLateUpdate=reinterpret_cast<decltype(instance.OnLateUpdate)>(dlsym(instance.Library,"BazzaltLateUpdateV1"));
        instance.OnApplicationEvent=reinterpret_cast<decltype(instance.OnApplicationEvent)>(dlsym(instance.Library,"BazzaltApplicationEventV1"));
#endif
        try{instance.Object=instance.Api->Create(binding.Entity.c_str());}catch(...){instance.Object=nullptr;}
        if(!instance.Object){error="Could not create script component: "+binding.TypeName;m_instances.push_back(instance);Stop();return false;}
#ifdef _WIN32
        auto asBehavior=reinterpret_cast<Behavior*(*)(void*)>(GetProcAddress(instance.Library,"BazzaltGetBehaviorV1"));
#else
        auto asBehavior=reinterpret_cast<Behavior*(*)(void*)>(dlsym(instance.Library,"BazzaltGetBehaviorV1"));
#endif
        if(asBehavior)instance.BehaviorObject=asBehavior(instance.Object);
        for(const auto& [name,value]:binding.Properties){
            bool valid=false;std::string reason;
            try{valid=!instance.Api->SetProperty||instance.Api->SetProperty(instance.Object,name.c_str(),value.c_str());}
            catch(const std::exception& failure){reason=failure.what();}catch(...){reason="Property setter threw an unknown exception";}
            if(!valid){error="Invalid property '"+name+"' on "+binding.TypeName+(reason.empty()?"":": "+reason);m_instances.push_back(instance);Stop();return false;}
        }
        m_instances.push_back(instance);
        try{BehaviorAccess::Register(binding.Entity,binding.TypeName,instance.BehaviorObject);}catch(const std::exception& failure){error=failure.what();Stop();return false;}
    }
    if(application)for(auto& instance:m_instances)if(!instance.OnApplicationEvent){error="Application script must be rebuilt with application event support";Stop();return false;}
    try{if(!application)for(auto& instance:m_instances){ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);instance.Api->OnCreate(instance.Object);}}catch(const std::exception& e){error=std::string("Script OnCreate failed: ")+e.what();Stop();return false;}catch(...){error="Script OnCreate failed";Stop();return false;}
    try{if(auto* scene=SceneManager::GetActiveScene()){
        auto previous=ApplicationEventsAccess::SetOwner(this);
        try{for(int i=0;i<6;++i)scene->GetPhysics().Subscribe(PhysicsEventType(i),[this](const CollisionEvent& event){
            for(std::size_t index=0;index<m_instances.size();++index){auto& instance=m_instances[index];auto* behavior=instance.BehaviorObject;if(!behavior||!behavior->Enabled)continue;auto entity=behavior->GetEntity();if(!entity)continue;auto id=entity.GetUUID();if(id!=event.BodyAUUID&&id!=event.BodyBUUID&&id!=event.ColliderAUUID&&id!=event.ColliderBUUID)continue;
                if(auto* scripts=entity.TryGetComponent<ScriptComponents>()){if(!scripts->Enabled)continue;bool enabled=true;for(const auto& script:scripts->Values)if(script.TypeName==instance.Api->TypeName)enabled=script.Enabled;if(!enabled)continue;}
                ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);
                switch(event.Type){case PhysicsEventType::CollisionEnter:behavior->OnCollisionEnter(event);break;case PhysicsEventType::CollisionStay:behavior->OnCollisionStay(event);break;case PhysicsEventType::CollisionExit:behavior->OnCollisionExit(event);break;case PhysicsEventType::TriggerEnter:behavior->OnTriggerEnter(event);break;case PhysicsEventType::TriggerStay:behavior->OnTriggerStay(event);break;case PhysicsEventType::TriggerExit:behavior->OnTriggerExit(event);break;}
            }
        });}catch(...){ApplicationEventsAccess::SetOwner(previous);throw;}ApplicationEventsAccess::SetOwner(previous);
    }}catch(const std::exception& failure){error=failure.what();Stop();return false;}
    return true;
}
void NativeScriptRuntime::Update(float deltaTime){for(auto& instance:m_instances){ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);instance.Api->OnUpdate(instance.Object,deltaTime);}}
void NativeScriptRuntime::FixedUpdate(float deltaTime){for(auto& instance:m_instances)if(instance.OnFixedUpdate){ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);instance.OnFixedUpdate(instance.Object,deltaTime);}}
void NativeScriptRuntime::LateUpdate(float deltaTime){for(auto& instance:m_instances)if(instance.OnLateUpdate){ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);instance.OnLateUpdate(instance.Object,deltaTime);}}
void NativeScriptRuntime::ApplicationEvent(const Bazzalt::ApplicationEvent& event){for(auto& instance:m_instances)if(instance.OnApplicationEvent){ModuleScope scope(instance.Lease,instance.Object,instance.Api->TypeName);instance.OnApplicationEvent(instance.Object,&event);}}
void NativeScriptRuntime::Stop() noexcept{ApplicationEventsAccess::DisconnectOwner(this);for(auto it=m_instances.rbegin();it!=m_instances.rend();++it)if(it->Object&&it->Api){ModuleScope scope(it->Lease,it->Object,it->Api->TypeName);BehaviorAccess::Remove(it->BehaviorObject);try{if(!it->Application)it->Api->OnDestroy(it->Object);}catch(...){}GraphicsAccess::DisconnectScope(it->Object);ApplicationEventsAccess::DisconnectOwner(it->Object);try{it->Api->Destroy(it->Object);}catch(...){}}m_instances.clear();}
}
