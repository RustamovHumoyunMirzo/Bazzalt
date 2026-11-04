#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include <vector>
namespace Bazzalt {
/// World-oriented, HDR, GGX-prefiltered cubemap capture. BoxSize is a world-space half extent.
struct ReflectionProbe : Component {
    unsigned Resolution=32;
    float Radius=20;
    float Intensity=1;
    float BlendDistance=1;
    bool BoxProjection=true;
    Vec3 BoxSize{10};
    bool Baked=false;
    Vec3 CapturePosition{};
    unsigned BakedResolution=0;
    std::vector<Vec3> Radiance;
    [[nodiscard]] BAZZALT_API bool IsValid() const;
};
}
