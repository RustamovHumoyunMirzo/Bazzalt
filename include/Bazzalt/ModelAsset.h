#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct ModelAssetNode {
    static constexpr std::uint32_t NoMesh = std::numeric_limits<std::uint32_t>::max();

    std::string Name;
    std::string StablePath;
    std::uint32_t SourceIndex = 0;
    std::uint32_t MeshIndex = NoMesh;
    Vec3 Position{};
    Quaternion Rotation{};
    Vec3 Scale{1.0f};
    std::vector<std::uint32_t> Children;

    [[nodiscard]] bool HasMesh() const { return MeshIndex != NoMesh; }
};

// One imported model asset. Nodes describe the editable hierarchy created for
// each scene instance; they are not separate assets.
struct ModelAsset {
    UUID Id{};
    std::string Name;
    std::vector<ModelAssetNode> Nodes;
    std::vector<std::uint32_t> Roots;
};

} // namespace Bazzalt
