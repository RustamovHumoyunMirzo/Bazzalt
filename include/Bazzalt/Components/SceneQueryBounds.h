#pragma once

#include <cstdint>

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

enum class SceneQueryShape { Box, Sphere };

// Lightweight authoring bounds for editor selection and gameplay scene queries.
// This is deliberately independent from any future physics collider component.
struct SceneQueryBounds : Component {
    SceneQueryShape Shape = SceneQueryShape::Box;
    Vec3 Center{};
    Vec3 Extents{0.5f};
    float Radius = 0.5f;
    std::uint32_t LayerMask = 0xffffffffu;
    bool Enabled = true;
};

} // namespace Bazzalt
