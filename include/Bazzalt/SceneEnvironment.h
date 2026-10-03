#pragma once
#include <cmath>
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {
enum class SceneEnvironmentMode { Map=0, Material=1 };
// Scene-owned authoring data; no graphics objects or editor state cross this API.
struct SceneEnvironment {
    SceneEnvironmentMode Mode=SceneEnvironmentMode::Map;
    UUID SourceAsset{};
    UUID MaterialAsset{};
    Vec4 ClearColor{0.055f,0.065f,0.085f,1.0f};
    Vec3 Rotation{}; // Radians; rotates image-based lighting.
    float Intensity=30000.0f; // Filament environment intensity, lux.
    bool ImageBasedLighting=true;
    bool SkyboxVisible=true;
    bool ShowSun=false;
    [[nodiscard]] bool IsValid() const {
        return (Mode==SceneEnvironmentMode::Map||Mode==SceneEnvironmentMode::Material)&&std::isfinite(Intensity)&&Intensity>=0 &&
            std::isfinite(Rotation.X)&&std::isfinite(Rotation.Y)&&std::isfinite(Rotation.Z)&&
            std::isfinite(ClearColor.X)&&std::isfinite(ClearColor.Y)&&std::isfinite(ClearColor.Z)&&std::isfinite(ClearColor.W)&&
            ClearColor.X>=0&&ClearColor.Y>=0&&ClearColor.Z>=0&&ClearColor.W>=0&&ClearColor.W<=1;
    }
};
}
