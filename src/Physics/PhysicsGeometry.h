#pragma once
#include "Bazzalt/Entity.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/Graphics.h"
#include <map>
#include <string>
#include <vector>
namespace Bazzalt::Runtime {
// Share filesystem revision lookups across all colliders in one synchronization.
using PhysicsMeshRevisions=std::map<UUID,std::string>;
std::string PhysicsMeshSignature(Entity entity,UUID asset,PhysicsMeshRevisions& revisions);
bool ReadPhysicsMesh(Entity entity,UUID asset,const std::string& signature,std::vector<Vec3>& vertices,std::vector<std::uint32_t>& indices);
struct SoftMeshPart {UUID Owner;std::uint32_t Node=0xffffffffu,Slot=0;std::vector<GraphicsVertex> Vertices;std::vector<std::uint32_t> Indices;};
// Render seams remain separate; the soft solver welds coincident particles.
bool ReadSoftMesh(Entity entity,std::vector<SoftMeshPart>& parts);
}
