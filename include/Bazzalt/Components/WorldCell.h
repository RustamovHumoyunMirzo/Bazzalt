#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Streaming.h"
#include <cmath>
namespace Bazzalt {
/// Author in the persistent scene. The entity's world transform places a unique scene chunk.
struct WorldCell : Component {
    UUID SceneAsset;
    Vec3 HalfExtents{128};
    float LoadDistance=100,UnloadDistance=150;
    int Priority=0;
    bool AlwaysLoaded=false;
    bool IsValid()const{return std::isfinite(HalfExtents.X)&&std::isfinite(HalfExtents.Y)&&std::isfinite(HalfExtents.Z)&&HalfExtents.X>=0&&HalfExtents.Y>=0&&HalfExtents.Z>=0&&std::isfinite(LoadDistance)&&std::isfinite(UnloadDistance)&&LoadDistance>=0&&UnloadDistance>LoadDistance;}
};
}
