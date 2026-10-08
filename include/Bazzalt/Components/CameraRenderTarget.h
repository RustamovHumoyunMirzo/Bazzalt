#pragma once
#include <cstdint>
#include "Bazzalt/Component.h"
namespace Bazzalt {
// Legacy transient target configuration, retained for existing scenes/scripts.
// New authoring uses Camera.RenderTarget with a Texture asset. When both are
// present, the explicit Texture takes precedence. Consume through RenderTexture,
// GuiImage.Camera, or Material.SetRenderTexture; no Game viewport is required.
// Resolution is bounded by the runtime. No GPU handle is public.
struct CameraRenderTarget : Component {
    std::uint32_t Width=512,Height=512;
};
}
