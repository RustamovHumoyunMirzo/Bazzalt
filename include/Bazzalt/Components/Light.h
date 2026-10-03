#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

enum class LightType { Directional, Sun, Point, Spot };

struct Light : Component {
    LightType Type = LightType::Point;
    Vec3 Color{1.0f};
    // Lux for directional/sun lights, lumens for point/spot lights.
    float Intensity = 1000.0f;
    // World-space spherical falloff radius for Point/Spot; ignored by Directional/Sun.
    float Range = 10.0f;
    // Cone half-angles in radians; only used by Spot.
    float InnerConeAngle = ToRadians(20.0f);
    float OuterConeAngle = ToRadians(30.0f);
    float SunAngularRadius = 0.00935f; // Radians; Sun only.
    float SunHaloSize = 10.0f;
    float SunHaloFalloff = 80.0f;
    bool CastShadows = true;
};

} // namespace Bazzalt
