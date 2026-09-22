#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct ModelNode : Component {
    UUID ModelAsset{};
    std::uint32_t SourceIndex = 0;
    std::uint32_t MeshIndex = std::numeric_limits<std::uint32_t>::max();
    std::string StablePath;
    bool HasMesh = false;
};

} // namespace Bazzalt
