#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include <array>
#include <cmath>
namespace Bazzalt {
/// Scene-owned diffuse irradiance in world-space, real, three-band spherical harmonics.
struct LightProbe : Component {
    float Radius=10.0f;
    float Intensity=1.0f;
    bool Baked=false;
    std::array<Vec3,9> Coefficients{};
    [[nodiscard]] bool IsValid() const {
        if(!std::isfinite(Radius)||Radius<=0||!std::isfinite(Intensity)||Intensity<0)return false;
        for(auto c:Coefficients)if(!std::isfinite(c.X)||!std::isfinite(c.Y)||!std::isfinite(c.Z)||!std::isfinite(c.X*Intensity)||!std::isfinite(c.Y*Intensity)||!std::isfinite(c.Z*Intensity))return false;
        return true;
    }
};
}
