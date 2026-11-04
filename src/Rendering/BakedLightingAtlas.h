#pragma once
#include "Bazzalt/Math.h"
#include <cstdint>
#include <vector>
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
struct BakedLightingAtlas {std::vector<float> Pixels;unsigned Width=1,Height=1;unsigned ReflectionCount=0;bool SpatialFallback=false;};
std::uint64_t BakedLightingFingerprint(Scene& scene);
BakedLightingAtlas BuildBakedLightingAtlas(Scene& scene,unsigned textureLimit=4096);
}
