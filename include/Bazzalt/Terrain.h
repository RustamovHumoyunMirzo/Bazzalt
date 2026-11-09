#pragma once
#include "Bazzalt/Components/Terrain.h"
#include "Bazzalt/Entity.h"
#include <optional>
namespace Bazzalt {
struct TerrainHit { Vec3 Position{},Normal{}; float Distance=0; };
struct TerrainVertex { Vec3 Position{},Normal{0,1,0};Vec2 UV{};Vec4 Color{1,1,1,1}; };
struct TerrainMesh { std::vector<TerrainVertex> Vertices;std::vector<std::uint32_t> Indices; };
// Stateless geometry/query utilities, suitable for tools, physics baking and jobs.
class BAZZALT_API TerrainGeometry final {
public:
    static TerrainMesh BuildChunk(const Terrain& terrain,std::uint32_t x,std::uint32_t z,std::uint32_t lod=0);
    static TerrainMesh BuildSpline(const Terrain& terrain,std::uint32_t spline);
    static std::optional<TerrainHit> Raycast(const Terrain& terrain,Vec3 origin,Vec3 direction,float maxDistance=100000);
};
}
