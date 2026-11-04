#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include <vector>
#include <string>
namespace Bazzalt {
/// Non-overlapping triangle charts. Renderer reconstructs chart UVs without consuming mesh UV sets.
struct Lightmap : Component {
    unsigned Resolution=128;
    unsigned Padding=2;
    bool Baked=false;
    unsigned BakedResolution=0, BakedPadding=0, ChartGrid=0;
    std::vector<Vec3> Geometry;
    std::vector<Vec3> Texels;
    std::string TransformSignature;
    [[nodiscard]] BAZZALT_API bool IsValid() const;
};
}
