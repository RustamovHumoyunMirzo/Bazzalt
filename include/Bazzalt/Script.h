#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include "Bazzalt/Component.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Input.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/ApplicationEvents.h"

namespace Bazzalt {
class ScriptRuntimeAccess;

// Public gameplay lifecycle. The editor/runtime owns construction and calls;
// game code never owns or overrides the application loop.
class Behavior : public Component {
public:
    virtual ~Behavior() = default;
    virtual void OnCreate() {}
    virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
    virtual void OnFixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
    virtual void OnLateUpdate(float deltaTime) { (void)deltaTime; }
    virtual void OnDestroy() {}
    virtual void OnCollisionEnter(const CollisionEvent&) {}
    virtual void OnCollisionStay(const CollisionEvent&) {}
    virtual void OnCollisionExit(const CollisionEvent&) {}
    virtual void OnTriggerEnter(const CollisionEvent&) {}
    virtual void OnTriggerStay(const CollisionEvent&) {}
    virtual void OnTriggerExit(const CollisionEvent&) {}
    [[nodiscard]] const std::string& GetEntityUUID() const { return m_entityUUID; }
    [[nodiscard]] Entity GetEntity() const {
        UUID id;
        auto* scene = SceneManager::GetActiveScene();
        return scene && UUID::TryParse(m_entityUUID, id) ? scene->GetEntity(id) : Entity{};
    }
private:
    friend class ScriptRuntimeAccess;
    std::string m_entityUUID;
};

class ScriptRuntimeAccess final {
public:
    static void Bind(Behavior& behavior,const char* entity){behavior.m_entityUUID=entity?entity:"";}
    static void BindTime(Detail::TimeState* state){Time::Bind(state);}
    static void BindInput(Detail::InputState* state){Input::Bind(state);}
};

// Call another native script through a shared public C++ interface/header.
// Returned pointers/references are valid only while that script instance lives.
class BAZZALT_API Scripts final {
public:
    static Behavior* Find(Entity entity,const std::string& typeName);
    template<class T> static T& Get(Entity entity,const std::string& typeName){
        auto* value=dynamic_cast<T*>(Find(entity,typeName));
        if(!value)throw std::logic_error("Script instance is missing or has an incompatible interface: "+typeName);
        return *value;
    }
};

enum class PropertyType : std::uint8_t { Boolean, Integer, Float, String, Vec2, Vec3, Vec4, Entity, Asset };

inline constexpr std::uint32_t ScriptAbiVersion = 2;
struct ScriptModuleApi {
    std::uint32_t AbiVersion;
    const char* TypeName;
    void* (*Create)(const char* entityUUID);
    void (*Destroy)(void*);
    void (*OnCreate)(void*);
    void (*OnUpdate)(void*, float);
    void (*OnDestroy)(void*);
    bool (*SetProperty)(void*, const char*, const char*);
};
using GetScriptModuleApi = const ScriptModuleApi* (*)();

} // namespace Bazzalt

// A source file declares one behavior. The editor parses the same declarations
// to build inspector metadata and generates the private module entry point.
#define COMPONENT(Name) class Name final : public ::Bazzalt::Behavior
#define PROPERTY(Type, Name, DefaultValue, ...) Type Name = DefaultValue;
