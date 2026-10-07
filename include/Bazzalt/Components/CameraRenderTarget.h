#pragma once
#include <cstdint>
#include "Bazzalt/Component.h"
namespace Bazzalt {
// Attach to a Camera entity to render offscreen; consume by GuiImage.Camera.
// Resolution is bounded by the runtime. No GPU handle is public.
struct CameraRenderTarget : Component {
    std::uint32_t Width=512,Height=512;
};
}
