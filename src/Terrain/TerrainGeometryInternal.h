#pragma once
#include "Bazzalt/Terrain.h"
namespace Bazzalt::Runtime {
// Caller validates once per terrain, rather than scanning all samples per chunk.
TerrainMesh BuildTerrainChunk(const Terrain&,unsigned x,unsigned z,unsigned lod);
}
