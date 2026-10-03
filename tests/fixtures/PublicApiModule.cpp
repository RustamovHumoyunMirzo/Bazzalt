#include <Bazzalt/Asset.h>
#include <Bazzalt/AssetManager.h>
#include <Bazzalt/Component.h>
#include <Bazzalt/Entity.h>
#include <Bazzalt/EntityReference.h>
#include <Bazzalt/Input.h>
#include <Bazzalt/Material.h>
#include <Bazzalt/Math.h>
#include <Bazzalt/ModelAsset.h>
#include <Bazzalt/PostProcessing.h>
#include <Bazzalt/Project.h>
#include <Bazzalt/Scene.h>
#include <Bazzalt/SceneEnvironment.h>
#include <Bazzalt/SceneManager.h>
#include <Bazzalt/SceneQuery.h>
#include <Bazzalt/Script.h>
#include <Bazzalt/Serialization.h>
#include <Bazzalt/Shader.h>
#include <Bazzalt/System.h>
#include <Bazzalt/Time.h>
#include <Bazzalt/UUID.h>
#include <Bazzalt/Components/Camera.h>
#include <Bazzalt/Components/Light.h>
#include <Bazzalt/Components/PrimitiveObject.h>
#include <cstring>
#include <stdexcept>

struct ScriptVelocity : Bazzalt::Component { std::string Label = "script-owned storage"; };
class ScriptSystem : public Bazzalt::System {
    void OnUpdate(Bazzalt::Scene&, float) override {}
    void OnCreate(Bazzalt::Scene& scene) override {
        scene.FindEntityByName("Cube").AddComponent<ScriptVelocity>();
    }
};
COMPONENT(PublicApiProbe) {
public:
    PROPERTY(Bazzalt::Entity, Cube, {})
    void OnCreate() override {
        auto* scene = Bazzalt::SceneManager::GetActiveScene();
        if (!scene || Cube != GetEntity()) throw std::runtime_error("Not the host scene/entity");
        scene->AddSystem<ScriptSystem>();
        if (!Cube.HasComponent<Bazzalt::PrimitiveObject>() || !Cube.HasComponent<Bazzalt::Camera>() || !Cube.HasComponent<Bazzalt::Light>())
            throw std::runtime_error("Engine component IDs differ across modules");
        Cube.SetComponentEnabled<Bazzalt::Light>(false);
        Cube.GetComponent<Bazzalt::Transform>().Position.X = 3.0f;
        Bazzalt::Time::SetTimeScale(0.5f);
    }
    void OnUpdate(float delta) override {
        auto world = Cube.GetWorldTransform();
        world.Translate({delta, 0.0f, 0.0f});
        if (!Cube.SetWorldTransform(world)) throw std::runtime_error("World transform update failed");
    }
};
namespace {
void* Create(const char* entity) { auto* value=new PublicApiProbe(); Bazzalt::ScriptRuntimeAccess::Bind(*value,entity); return value; }
void Destroy(void* value) { delete static_cast<PublicApiProbe*>(value); }
void CreateCallback(void* value) { static_cast<PublicApiProbe*>(value)->OnCreate(); }
void Update(void* value,float delta) { static_cast<PublicApiProbe*>(value)->OnUpdate(delta); }
void DestroyCallback(void* value) { static_cast<PublicApiProbe*>(value)->OnDestroy(); }
bool Set(void* value,const char* name,const char* text) {
    Bazzalt::UUID id;
    if(std::strcmp(name,"Cube") || !Bazzalt::UUID::TryParse(text,id)) return false;
    static_cast<PublicApiProbe*>(value)->Cube=Bazzalt::SceneManager::GetActiveScene()->GetEntity(id);
    return true;
}
const Bazzalt::ScriptModuleApi Api{Bazzalt::ScriptAbiVersion,"PublicApiProbe",Create,Destroy,CreateCallback,Update,DestroyCallback,Set};
}
#ifdef _WIN32
#define SCRIPT_EXPORT __declspec(dllexport)
#else
#define SCRIPT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" SCRIPT_EXPORT const Bazzalt::ScriptModuleApi* BazzaltGetScriptModuleV1() { return &Api; }
