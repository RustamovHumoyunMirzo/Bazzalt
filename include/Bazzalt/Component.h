#pragma once

#include <cstdint>

namespace Bazzalt {

// Marker base for components managed by Bazzalt's ECS. It intentionally has
// no virtual functions or state so components remain lightweight data types.
struct Component {};

// Component lookup is local by default. Games may explicitly specialize this
// trait for data that should fall back to the nearest ancestor.
enum class ComponentInheritanceMode : std::uint8_t {
    None,
    NearestAncestor,
    Composed
};

template<typename ComponentType>
struct ComponentInheritance {
    static constexpr ComponentInheritanceMode Mode = ComponentInheritanceMode::None;
};

} // namespace Bazzalt
