#pragma once
#include <cstdint>
#include <memory>
#include <filesystem>
#include <vector>
#include <array>
#include <utility>
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
    struct SkinVertex {std::array<unsigned,8> Joints{};std::array<float,8> Weights{};};
    std::vector<std::array<SkinVertex,3>> Skin;
    std::vector<std::vector<std::array<Vec3,3>>> MorphPositions;
    struct InfluenceBounds {unsigned Joint=0;Vec3 Min{},Max{};std::vector<std::pair<Vec3,Vec3>> Morph;};
    std::vector<InfluenceBounds> Influences;
    void Build();
    void Refit();
    void Deform(const ModelGeometry& source,const std::vector<Mat4>& palette,const std::vector<float>& weights);
    std::pair<Vec3,Vec3> DeformedBounds(const std::vector<Mat4>& palette,const std::vector<float>& weights)const;
    bool Raycast(Vec3 origin,Vec3 direction,float& distance,float determinantEpsilon=1e-10f) const;
};
struct ModelGeometryAsset {
    struct Skin {std::vector<unsigned> Joints;std::vector<Mat4> InverseBind;};
    std::vector<std::shared_ptr<ModelGeometry>> SourceNodes;
    std::vector<int> NodeSkins;std::vector<Skin> Skins;
    std::vector<std::vector<float>> NodeWeights;
};
std::shared_ptr<ModelGeometryAsset> ReadModelGeometry(const std::vector<std::uint8_t>& preparedGltf,const std::filesystem::path& source={});
}
