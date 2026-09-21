#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

struct Vignette : Component {
    bool Enabled = true;
    Vec4 Color{0.0f, 0.0f, 0.0f, 1.0f};
    float Intensity = 0.35f;
    float Smoothness = 0.35f;
    float Roundness = 1.0f;
};

} // namespace Bazzalt
