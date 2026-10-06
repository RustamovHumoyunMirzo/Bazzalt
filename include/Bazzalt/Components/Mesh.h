#pragma once
#include <algorithm>

#include <cstdint>
#include <limits>
#include <vector>

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Material.h"

namespace Bazzalt {

struct Mesh : Component {
    static constexpr std::uint32_t EntireAsset = std::numeric_limits<std::uint32_t>::max();
    UUID MeshAsset{};
    // Optional per-entity override. Zero preserves the imported model's materials.
    UUID MaterialAsset{};
    // glTF node index, or EntireAsset for a standalone mesh / complete model.
    std::uint32_t ModelNodeIndex = EntireAsset;
    std::vector<UUID> Materials;
    std::uint8_t LayerMask = 0xff;
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
    void SetMaterial(Material material) { MaterialAsset=material.GetAssetUUID(); }
    void SetMaterial(std::size_t slot, Material material) {
        if(slot>=4096)throw std::out_of_range("Material slot exceeds the supported range");
        if(MaterialAsset)std::fill(Materials.begin(),Materials.end(),MaterialAsset);
        if(Materials.size()<=slot)Materials.resize(slot+1,MaterialAsset);
        MaterialAsset={};Materials[slot]=material.GetAssetUUID();
    }
    [[nodiscard]] Material GetMaterial() const { return Material::Load(MaterialAsset); }
    [[nodiscard]] Material GetMaterial(std::size_t slot) const { return Material::Load(MaterialAsset?MaterialAsset:slot<Materials.size()?Materials[slot]:UUID{}); }
    [[nodiscard]] std::size_t GetMaterialCount() const { return Materials.empty()?(MaterialAsset?1u:0u):Materials.size(); }
    void ClearMaterial(std::size_t slot) { SetMaterial(slot,Material{}); }
    void ClearMaterial() { MaterialAsset={};Materials.clear(); }
};

} // namespace Bazzalt
