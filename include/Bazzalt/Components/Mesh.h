#pragma once

#include <cstdint>
#include <vector>

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct Mesh : Component {
    UUID MeshAsset{};
    std::vector<UUID> Materials;
    std::uint8_t LayerMask = 0xff;
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
};

} // namespace Bazzalt
