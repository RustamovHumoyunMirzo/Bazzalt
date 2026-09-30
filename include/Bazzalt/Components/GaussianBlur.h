#pragma once

#include "Bazzalt/Component.h"

namespace Bazzalt {

struct GaussianBlur : Component {
    // Blur radius multiplier in pixels. Zero produces an unchanged image.
    float Size = 1.0f;
};

} // namespace Bazzalt
