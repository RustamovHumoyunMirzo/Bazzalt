#pragma once
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
struct LODMask {float Minimum=0,Maximum=1;};
struct LODContribution {UUID Group;LODMask Mask;bool Selected=true,Fading=false;};
struct LODBlend {int From=-2,To=-2;float Progress=1;double LastTime=0;std::uint64_t Signature=0;};
struct LODViewState {
    std::unordered_map<UUID,int> Levels;
    std::unordered_set<UUID> Hidden,HardHidden,Warnings,UnsupportedGroups,FallbackWarnings;
    std::unordered_map<UUID,LODBlend> Blends;
    std::unordered_map<UUID,std::vector<LODContribution>> Contributions;
    std::unordered_map<UUID,LODMask> Masks;
    std::uint64_t Frame=0;
    bool HasFades=false;
    void RejectUnsupported(UUID owner,bool supported);
    void Resolve();
};
void EvaluateLOD(Scene& scene,Vec3 eye,float projectionYScale,bool orthographic,LODViewState& state,double now=-1);
}
