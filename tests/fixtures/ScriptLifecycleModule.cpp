#include <Bazzalt/GlobalEvent.h>
#include <Bazzalt/Components/BehaviorTreeAgent.h>
#include <Bazzalt/Systems/BehaviorTreeSystem.h>
#include <Bazzalt/Script.h>
#include <Bazzalt/Material.h>
#include <cstring>
#include <fstream>
#include <string>

COMPONENT(LifecycleProbe) {
public:
    PROPERTY(std::string, LogPath, "")
    PROPERTY(Bazzalt::Material, Surface, {})
    PROPERTY(Bazzalt::GlobalEvent, Signal, {})
    int ReloadState=3;
    std::string OnSaveReloadState()const override{return std::to_string(ReloadState);}
    void OnRestoreReloadState(const std::string& state)override{ReloadState=std::stoi(state);}
    void Write(const char* value){std::ofstream(LogPath,std::ios::app)<<value<<':'<<GetEntityUUID()<<'\n';}
    void BindAI(){
        auto entity=GetEntity();if(!entity||!entity.HasComponent<Bazzalt::BehaviorTreeAgent>())return;
        auto& ai=entity.GetScene()->GetSystem<Bazzalt::BehaviorTreeSystem>();
        ai.SetHandlers(entity,[this](const std::string&,Bazzalt::AI::Context&,bool cancel){Write(cancel?"ai_cancel":"ai_tick");return Bazzalt::AI::Status::Running;},{});
        ai.Action=[this](const std::string&,Bazzalt::AI::Context&,bool cancel){Write(cancel?"default_cancel":"default_tick");return Bazzalt::AI::Status::Running;};
    }
    void OnCreate() override { ReloadState=99;if(!Bazzalt::Detail::BoundMaterialServices)throw std::runtime_error("Material host service was not bound");if(Surface.IsValid())Surface.SetFloat("roughness",0.1f);Bazzalt::Time::SetSlowMotion(0.25f);if(Signal.IsValid())Signal.Fire({5.0,std::string("hit")});Write("create");BindAI(); }
    void OnUpdate(float delta) override { if(Bazzalt::Time::GetDeltaTime()!=delta)throw std::runtime_error("Module clock was not shared");Write("update");if(Bazzalt::Input::GetKeyDown(Bazzalt::KeyCode::W))Write("input_w"); }
    void OnFixedUpdate(float delta) override { if(!Bazzalt::Time::IsInFixedTimeStep()||Bazzalt::Time::GetDeltaTime()!=delta)throw std::runtime_error("Module fixed clock was not shared");Write("fixed"); }
    void OnTriggerEnter(const Bazzalt::CollisionEvent& event) override { if(event.Contacts.empty())throw std::runtime_error("Missing trigger contact manifold");Write("trigger"); }
    void OnDestroy() override { Write("destroy"); }
    void OnReload() override { if(ReloadState!=99)throw std::runtime_error("Custom state was lost");Write("reload");BindAI(); }
    void OnLateUpdate(float) override { Write("late"); }
};

namespace {
void* Create(const char* entity){auto* value=new LifecycleProbe();Bazzalt::ScriptRuntimeAccess::Bind(*value,entity);return value;}
void Destroy(void* value){delete static_cast<LifecycleProbe*>(value);}
void OnCreate(void* value){static_cast<LifecycleProbe*>(value)->OnCreate();}
void OnUpdate(void* value,float delta){static_cast<LifecycleProbe*>(value)->OnUpdate(delta);}
void OnDestroy(void* value){static_cast<LifecycleProbe*>(value)->OnDestroy();}
bool SetProperty(void* value,const char* name,const char* text){if(!std::strcmp(name,"Signal")){Bazzalt::UUID id;if(!Bazzalt::UUID::TryParse(text,id))return false;static_cast<LifecycleProbe*>(value)->Signal=Bazzalt::GlobalEvent::Load(id);return true;}if(!std::strcmp(name,"Surface")){Bazzalt::UUID id;if(!Bazzalt::UUID::TryParse(text,id))return false;static_cast<LifecycleProbe*>(value)->Surface=Bazzalt::Material::Load(id);return true;}if(std::strcmp(name,"LogPath"))return false;static_cast<LifecycleProbe*>(value)->LogPath=text;return true;}
const Bazzalt::ScriptModuleApi Api{Bazzalt::ScriptAbiVersion,"LifecycleProbe",Create,Destroy,OnCreate,OnUpdate,OnDestroy,SetProperty};
}
#ifdef _WIN32
#define SCRIPT_EXPORT __declspec(dllexport)
#else
#define SCRIPT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" SCRIPT_EXPORT const Bazzalt::ScriptModuleApi* BazzaltGetScriptModuleV1(){return &Api;}
extern "C" SCRIPT_EXPORT void BazzaltBindTimeV1(Bazzalt::Detail::TimeState* state){Bazzalt::ScriptRuntimeAccess::BindTime(state);}
extern "C" SCRIPT_EXPORT void BazzaltBindInputV1(Bazzalt::Detail::InputState* state){Bazzalt::ScriptRuntimeAccess::BindInput(state);}
extern "C" SCRIPT_EXPORT void BazzaltBindMaterialsV1(Bazzalt::Detail::MaterialServices* services){Bazzalt::Detail::BoundMaterialServices=services;}
extern "C" SCRIPT_EXPORT void BazzaltFixedUpdateV1(void* value,float delta){static_cast<LifecycleProbe*>(value)->OnFixedUpdate(delta);}
extern "C" SCRIPT_EXPORT void BazzaltLateUpdateV1(void* value,float delta){static_cast<LifecycleProbe*>(value)->OnLateUpdate(delta);}
extern "C" SCRIPT_EXPORT Bazzalt::Behavior* BazzaltGetBehaviorV1(void* value){return static_cast<LifecycleProbe*>(value);}

extern "C" SCRIPT_EXPORT void BazzaltVisitPropertiesV1(void* value,void* context,void(*visit)(void*,const char*,const char*,const char*)) noexcept(false){auto* self=static_cast<LifecycleProbe*>(value);visit(context,"LogPath","std::string",self->LogPath.c_str());}
extern "C" SCRIPT_EXPORT void BazzaltReloadV1(void* value) noexcept(false){static_cast<LifecycleProbe*>(value)->OnReload();}

extern "C" SCRIPT_EXPORT const char* BazzaltSaveReloadStateV1(void* value) noexcept(false){static thread_local std::string state;state=static_cast<LifecycleProbe*>(value)->OnSaveReloadState();return state.c_str();}
extern "C" SCRIPT_EXPORT void BazzaltRestoreReloadStateV1(void* value,const char* state) noexcept(false){static_cast<LifecycleProbe*>(value)->OnRestoreReloadState(state);}
