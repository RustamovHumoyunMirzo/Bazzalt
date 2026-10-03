#pragma once

#include <cstdint>
#include <string>
#include "Bazzalt/Component.h"
#include "Bazzalt/Time.h"
#include "Bazzalt/Input.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/SceneManager.h"

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
    virtual void OnDestroy() {}
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

enum class PropertyType : std::uint8_t { Boolean, Integer, Float, String, Vec2, Vec3, Vec4, Entity, Asset };

inline constexpr std::uint32_t ScriptAbiVersion = 1;
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
