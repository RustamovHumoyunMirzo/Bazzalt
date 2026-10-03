#include <Bazzalt/Script.h>
#include <Bazzalt/Material.h>
#include <cstring>
#include <fstream>
#include <string>

COMPONENT(LifecycleProbe) {
public:
    PROPERTY(std::string, LogPath, "")
    PROPERTY(Bazzalt::Material, Surface, {})
    void Write(const char* value){std::ofstream(LogPath,std::ios::app)<<value<<':'<<GetEntityUUID()<<'\n';}
    void OnCreate() override { if(!Bazzalt::Detail::BoundMaterialServices)throw std::runtime_error("Material host service was not bound");if(Surface.IsValid())Surface.SetFloat("roughness",0.1f);Bazzalt::Time::SetSlowMotion(0.25f);Write("create"); }
    void OnUpdate(float delta) override { if(Bazzalt::Time::GetDeltaTime()!=delta)throw std::runtime_error("Module clock was not shared");Write("update"); }
    void OnFixedUpdate(float delta) override { if(!Bazzalt::Time::IsInFixedTimeStep()||Bazzalt::Time::GetDeltaTime()!=delta)throw std::runtime_error("Module fixed clock was not shared");Write("fixed"); }
    void OnDestroy() override { Write("destroy"); }
};

namespace {
void* Create(const char* entity){auto* value=new LifecycleProbe();Bazzalt::ScriptRuntimeAccess::Bind(*value,entity);return value;}
void Destroy(void* value){delete static_cast<LifecycleProbe*>(value);}
void OnCreate(void* value){static_cast<LifecycleProbe*>(value)->OnCreate();}
void OnUpdate(void* value,float delta){static_cast<LifecycleProbe*>(value)->OnUpdate(delta);}
void OnDestroy(void* value){static_cast<LifecycleProbe*>(value)->OnDestroy();}
bool SetProperty(void* value,const char* name,const char* text){if(!std::strcmp(name,"Surface")){Bazzalt::UUID id;if(!Bazzalt::UUID::TryParse(text,id))return false;static_cast<LifecycleProbe*>(value)->Surface=Bazzalt::Material::Load(id);return true;}if(std::strcmp(name,"LogPath"))return false;static_cast<LifecycleProbe*>(value)->LogPath=text;return true;}
const Bazzalt::ScriptModuleApi Api{Bazzalt::ScriptAbiVersion,"LifecycleProbe",Create,Destroy,OnCreate,OnUpdate,OnDestroy,SetProperty};
}
#ifdef _WIN32
#define SCRIPT_EXPORT __declspec(dllexport)
#else
#define SCRIPT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" SCRIPT_EXPORT const Bazzalt::ScriptModuleApi* BazzaltGetScriptModuleV1(){return &Api;}
extern "C" SCRIPT_EXPORT void BazzaltBindTimeV1(Bazzalt::Detail::TimeState* state){Bazzalt::ScriptRuntimeAccess::BindTime(state);}
extern "C" SCRIPT_EXPORT void BazzaltBindMaterialsV1(Bazzalt::Detail::MaterialServices* services){Bazzalt::Detail::BoundMaterialServices=services;}
extern "C" SCRIPT_EXPORT void BazzaltFixedUpdateV1(void* value,float delta){static_cast<LifecycleProbe*>(value)->OnFixedUpdate(delta);}
