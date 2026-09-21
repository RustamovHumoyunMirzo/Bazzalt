#pragma once

#include <cstdint>

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

enum class CameraProjection { Perspective, Orthographic };
enum class CameraAspectMode { Automatic, Fixed };
enum class AntiAliasing { None, FXAA, TAA };
enum class ToneMapping { Linear, Filmic, ACES };

// Normalized target coordinates. X/Y use a bottom-left origin to match the
// renderer while Width/Height are fractions of the presentation surface.
struct CameraViewport {
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 1.0f;
    float Height = 1.0f;

    [[nodiscard]] static constexpr CameraViewport FullScreen() { return {}; }
    [[nodiscard]] static constexpr CameraViewport LeftHalf() { return {0, 0, 0.5f, 1}; }
    [[nodiscard]] static constexpr CameraViewport RightHalf() { return {0.5f, 0, 0.5f, 1}; }
    [[nodiscard]] static constexpr CameraViewport BottomHalf() { return {0, 0, 1, 0.5f}; }
    [[nodiscard]] static constexpr CameraViewport TopHalf() { return {0, 0.5f, 1, 0.5f}; }

    [[nodiscard]] static constexpr CameraViewport Grid(
        std::uint32_t column, std::uint32_t row,
        std::uint32_t columns, std::uint32_t rows) {
        if (columns == 0 || rows == 0 || column >= columns || row >= rows)
            return {0, 0, 0, 0};
        const float width = 1.0f / static_cast<float>(columns);
        const float height = 1.0f / static_cast<float>(rows);
        return {static_cast<float>(column) * width,
                static_cast<float>(row) * height, width, height};
    }
};

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
    CameraAspectMode AspectMode = CameraAspectMode::Automatic;
    CameraViewport Viewport{};
    // Lower priorities render first; equal priorities are ordered by UUID.
    int Priority = 0;
    bool Active = true;
    Vec4 ClearColor{0.02f, 0.02f, 0.025f, 1.0f};
    CameraPostProcessing PostProcessing{};
};

} // namespace Bazzalt
