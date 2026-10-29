#pragma once
#include <cstdint>
#include <memory>
#include <filesystem>
#include <vector>
#include "Bazzalt/Math.h"

namespace Bazzalt::Runtime {
// CPU-side geometry shared by all instances of an imported mesh.
// No renderer types or asset buffers escape into the public API.
struct ModelGeometry {
    struct Triangle { Vec3 A,B,C;Vec3 NA{},NB{},NC{};Vec2 UVA{},UVB{},UVC{};std::uint32_t Slot=0; };
    struct Node { Vec3 Min,Max; std::uint32_t Begin=0,Count=0,Left=0,Right=0; };
    std::vector<Triangle> Triangles;
    std::vector<Node> Nodes;
    std::vector<std::uint32_t> Order;
    void Build();
    bool Raycast(Vec3 origin,Vec3 direction,float& distance,float determinantEpsilon=1e-10f) const;
};
struct ModelGeometryAsset { std::vector<std::shared_ptr<ModelGeometry>> SourceNodes; };
std::shared_ptr<ModelGeometryAsset> ReadModelGeometry(const std::vector<std::uint8_t>& preparedGltf,const std::filesystem::path& source={});
}
