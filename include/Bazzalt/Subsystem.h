#pragma once
#include "Bazzalt/ApplicationControl.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Scene.h"
#include <stdexcept>
namespace Bazzalt {
// Original persisted IDs remain stable; scheduling uses lifecycle order.
enum class SubsystemLevel { Boot=0, Window=1, Initialization=2, Content=3, Ready=4,
    BeforeInput=5,Input=6,AfterInput=7,BeforeAudio=8,Audio=9,AfterAudio=10,
    BeforePhysics=11,Physics=12,AfterPhysics=13 };
// Not a Component or Behavior. Deliberately has no GetEntity().
class Subsystem {
public:
    virtual ~Subsystem()=default;
    virtual void OnRegister()=0;
    virtual void OnUnregister() {}
protected:
    BAZZALT_API static Scene& GetScene();
};
inline constexpr std::uint32_t SubsystemAbiVersion=1;
struct SubsystemModuleApi {
    std::uint32_t AbiVersion;
    const char* TypeName;
    void* (*Create)();
    void (*Destroy)(void*);
    void (*Register)(void*);
    void (*Unregister)(void*);
};
using GetSubsystemModuleApi=const SubsystemModuleApi* (*)();
class Subsystems final {
public:
    BAZZALT_API static Subsystem* Find(const std::string& name);
    template<class T> static T& Get(const std::string& name){
        auto* value=dynamic_cast<T*>(Find(name));
        if(!value)throw std::logic_error("Subsystem is not loaded or has an incompatible interface: "+name);
        return *value;
    }
};
}
#define SUBSYSTEM(Name) class Name final : public ::Bazzalt::Subsystem
