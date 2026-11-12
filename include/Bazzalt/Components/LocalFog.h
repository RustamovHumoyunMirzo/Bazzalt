#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Fog.h"

namespace Bazzalt {
// Oriented box in the owning entity's local space. Its world transform includes
// parents. Overlapping media add extinction and scattering before integration.
struct LocalFog : Component {
    Vec3 Size{10,10,10};
    float BlendDistance = 1;
    FogSettings Fog = [] { FogSettings value; value.Mode=FogMode::Volumetric;
        value.Density=.05f; value.StartDistance=0; value.HeightFalloff=0;
        value.NoiseAmount=0; value.AffectSky=true; return value; }();
    bool IsValid() const {
        return Fog.IsValid() && std::isfinite(Size.X) && std::isfinite(Size.Y) && std::isfinite(Size.Z)
            && Size.X>0 && Size.Y>0 && Size.Z>0 && std::isfinite(BlendDistance) && BlendDistance>=0;
    }
};
}
