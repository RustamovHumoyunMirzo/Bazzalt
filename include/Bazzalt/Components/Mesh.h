#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct Mesh : Component {
    static constexpr std::uint32_t EntireAsset = std::numeric_limits<std::uint32_t>::max();
    UUID MeshAsset{};
    // glTF node index, or EntireAsset for a standalone mesh / complete model.
    std::uint32_t ModelNodeIndex = EntireAsset;
    std::vector<UUID> Materials;
    std::uint8_t LayerMask = 0xff;
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
};

} // namespace Bazzalt
