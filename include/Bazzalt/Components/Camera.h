#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

enum class CameraProjection { Perspective, Orthographic };
enum class AntiAliasing { None, FXAA, TAA };
enum class ToneMapping { Linear, Filmic, ACES };

struct CameraPostProcessing {
    bool Enabled = true;
    bool Bloom = true;
    bool AmbientOcclusion = true;
    AntiAliasing AntiAliasingMode = AntiAliasing::TAA;
    ToneMapping ToneMappingMode = ToneMapping::ACES;
    // Exposure compensation in EV stops (0 is neutral).
    float Exposure = 0.0f;
};

struct Camera : Component {
    CameraProjection Projection = CameraProjection::Perspective;
    float VerticalFieldOfView = ToRadians(60.0f);
    float OrthographicSize = 10.0f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    float AspectRatio = 16.0f / 9.0f;
    int Priority = 0;
    bool Active = true;
    Vec4 ClearColor{0.02f, 0.02f, 0.025f, 1.0f};
    CameraPostProcessing PostProcessing{};
};

} // namespace Bazzalt
