#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
// Procedural XY cloth. Pin flags: left=1,right=2,top=4,bottom=8.
struct Cloth : Component {
    float Width=2,Height=2,Mass=1,Compliance=0,ShearCompliance=0,BendCompliance=.0001f;
    float Damping=.1f,GravityFactor=1,Friction=.5f,Restitution=0,MaxVelocity=100;
    Vec3 Wind{};
    unsigned SegmentsX=16,SegmentsY=16,PinEdges=4,Iterations=5,Layer=0;
    UUID MaterialAsset{};
    bool AllowSleeping=true,CastShadows=true;
    bool IsValid() const {
        for(float v:{Width,Height,Mass,Compliance,ShearCompliance,BendCompliance,Damping,GravityFactor,Friction,Restitution,MaxVelocity,Wind.X,Wind.Y,Wind.Z})if(!std::isfinite(v))return false;
        return Width>0&&Height>0&&Mass>0&&SegmentsX>=1&&SegmentsX<=128&&SegmentsY>=1&&SegmentsY<=128&&(SegmentsX+1)*(SegmentsY+1)<=16384&&PinEdges<=15&&Iterations>=1&&Iterations<=64&&Layer<32&&Compliance>=0&&ShearCompliance>=0&&BendCompliance>=0&&Damping>=0&&Friction>=0&&Restitution>=0&&Restitution<=1&&MaxVelocity>0;
    }
};
}
