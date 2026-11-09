#pragma once
#include "Bazzalt/System.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Components/Terrain.h"
#include "Rendering/RenderAssets.h"
#include <unordered_map>
#include <tuple>
#include <array>
#include <filesystem>
namespace Bazzalt::Runtime {
class RenderBackend;
class TerrainSystem final : public ComponentSystem<Transform,Terrain> {
public:
    explicit TerrainSystem(RenderBackend& backend):m_backend(backend){}
protected:
    void OnUpdate(Scene&,float) override;
    void OnDestroy(Scene&) override;
private:
    struct Chunk {std::uint64_t Fingerprint=0;unsigned LOD=0;std::vector<RenderAssets::Handle> Handles;};
    using VegetationKey=std::tuple<UUID,UUID,bool,std::array<std::uint32_t,8>,std::filesystem::path>;
    struct Resource {std::uint64_t Revision=0,ValidationRevision=0;bool Valid=false;std::vector<Chunk> Chunks;std::vector<RenderAssets::Handle> Surfaces;std::vector<RenderAssets::Handle> Vegetation;std::vector<VegetationKey> VegetationKeys;std::vector<std::filesystem::path> ModelPaths;};
    void Destroy(Resource&);
    RenderBackend& m_backend;
    std::unordered_map<UUID,Resource> m_resources;
};
}
