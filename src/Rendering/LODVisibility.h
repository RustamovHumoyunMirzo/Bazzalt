#pragma once
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt { class Scene; }
#include <unordered_set>
#include <unordered_map>
namespace Bazzalt::Runtime {
struct LODViewState {
    std::unordered_map<UUID,int> Levels;
    std::unordered_set<UUID> Hidden,Warnings;
    std::uint64_t Frame=0;
};
void EvaluateLOD(Scene& scene,Vec3 eye,float projectionYScale,bool orthographic,LODViewState& state);
}
