#pragma once
#include "Bazzalt/EcsConfig.h"

#include <cstdint>

namespace Bazzalt {

// Common non-virtual component state. Components stay lightweight data types,
// while every user-defined component receives the same enable/disable API.
class Component {
public:
    [[nodiscard]] bool IsEnabled() const { return Enabled; }
    void SetEnabled(bool enabled) { Enabled = enabled; }

    // Public for plain-data serialization and tooling. Prefer the methods on
    // Entity when changing this state in gameplay code.
    bool Enabled = true;
};

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
