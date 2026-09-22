#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct ModelInstance : Component {
    UUID ModelAsset{};
};

} // namespace Bazzalt
