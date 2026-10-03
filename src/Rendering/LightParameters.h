#pragma once
#include <algorithm>
#include <cmath>
#include "Bazzalt/Components/Light.h"

namespace Bazzalt::Runtime {
// One effective parameter contract for renderer, guides, and editor inspection.
inline Light SanitizeLight(Light light) {
    const auto finite=[](float value,float fallback){return std::isfinite(value)?value:fallback;};
    if(static_cast<int>(light.Type)<0||static_cast<int>(light.Type)>3)light.Type=LightType::Point;
    light.Color={std::max(0.0f,finite(light.Color.X,1)),std::max(0.0f,finite(light.Color.Y,1)),std::max(0.0f,finite(light.Color.Z,1))};
    light.Intensity=std::max(0.0f,finite(light.Intensity,0));
    light.Range=std::max(.001f,finite(light.Range,10));
    light.OuterConeAngle=std::clamp(finite(light.OuterConeAngle,ToRadians(30)),ToRadians(.5f),Pi*.5f);
    light.InnerConeAngle=std::clamp(finite(light.InnerConeAngle,ToRadians(20)),ToRadians(.5f),light.OuterConeAngle);
    light.SunAngularRadius=std::clamp(finite(light.SunAngularRadius,.00935f),ToRadians(.25f),ToRadians(20));
    light.SunHaloSize=std::max(1.0f,finite(light.SunHaloSize,10));
    light.SunHaloFalloff=std::max(1.0f,finite(light.SunHaloFalloff,80));
    return light;
}
}
