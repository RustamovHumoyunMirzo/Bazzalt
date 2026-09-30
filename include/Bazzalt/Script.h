#pragma once

#include <cstdint>
#include "Bazzalt/Component.h"

namespace Bazzalt {

// Public gameplay lifecycle. The editor/runtime owns construction and calls;
// game code never owns or overrides the application loop.
class Behavior : public Component {
public:
    virtual ~Behavior() = default;
    virtual void OnCreate() {}
    virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
    virtual void OnDestroy() {}
};

enum class PropertyType : std::uint8_t { Boolean, Integer, Float, String, Vec2, Vec3, Vec4, Entity, Asset };

} // namespace Bazzalt

// A source file declares one behavior. The editor parses the same declarations
// to build inspector metadata and generates the private module entry point.
#define COMPONENT(Name) class Name final : public ::Bazzalt::Behavior
#define PROPERTY(Type, Name, DefaultValue, ...) Type Name = DefaultValue;

