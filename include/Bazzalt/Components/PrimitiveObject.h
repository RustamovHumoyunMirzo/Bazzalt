#pragma once

#include <cstdint>

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

enum class PrimitiveShape : std::uint8_t { Cube, Sphere, Cylinder, Capsule, Plane, Cone, Torus };

// A renderer-owned procedural mesh. It requires no asset import and is serialized
// entirely with the scene, making it suitable for blocking, prototyping and tools.
struct PrimitiveObject : Component {
    PrimitiveShape Shape = PrimitiveShape::Cube;
    Vec3 Size{1.0f, 1.0f, 1.0f};
    float Radius = 0.5f;
    float Height = 2.0f;
    float Width = 1.0f;
    float Depth = 1.0f;
    float MajorRadius = 0.75f;
    float MinorRadius = 0.25f;
    std::uint32_t Segments = 32;
    std::uint32_t Rings = 16;
    Vec4 Color{0.72f, 0.72f, 0.75f, 1.0f};
    UUID MaterialAsset{};
    std::uint8_t LayerMask = 0xff;
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
};

} // namespace Bazzalt
