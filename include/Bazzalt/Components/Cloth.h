#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
enum class ClothBendMode : unsigned char { None, Distance, Dihedral };
// Deforms the attached primitive/mesh (or the model subtree). No extra plane.
// Pin flags: left=1,right=2,top=4,bottom=8, evaluated on source bounds.
// Width/Height/SegmentsX/SegmentsY are retained for old scene compatibility;
// geometry and resolution now come from the source PrimitiveObject or Mesh.
struct Cloth : Component {
    float Width=2,Height=2,Mass=1,Compliance=.0001f,ShearCompliance=.0001f,BendCompliance=.001f;
    ClothBendMode BendMode=ClothBendMode::None;
    // Maximum adaptive midpoint-refinement passes, bounded by the particle budget.
    unsigned SubdivisionLevel=2;
    // Pressure is nRT, as in Jolt: zero is an uninflated cloth shell.
    float Pressure=0,CollisionRadius=.01f;
    float Damping=.1f,GravityFactor=1,Friction=.5f,Restitution=0,MaxVelocity=100;
    Vec3 Wind{};
    unsigned SegmentsX=16,SegmentsY=16,PinEdges=4,Iterations=5,Layer=0;
    UUID MaterialAsset{};
    bool AllowSleeping=true,CastShadows=true;
    bool IsValid() const {
        for(float v:{Width,Height,Mass,Compliance,ShearCompliance,BendCompliance,Pressure,CollisionRadius,Damping,GravityFactor,Friction,Restitution,MaxVelocity,Wind.X,Wind.Y,Wind.Z})if(!std::isfinite(v))return false;
        return Width>0&&Height>0&&Mass>0&&SegmentsX>=1&&SegmentsX<=128&&SegmentsY>=1&&SegmentsY<=128&&(SegmentsX+1)*(SegmentsY+1)<=16384&&int(BendMode)<=2&&SubdivisionLevel<=4&&Pressure>=0&&CollisionRadius>=0&&PinEdges<=15&&Iterations>=1&&Iterations<=64&&Layer<32&&Compliance>=0&&ShearCompliance>=0&&BendCompliance>=0&&Damping>=0&&Friction>=0&&Restitution>=0&&Restitution<=1&&MaxVelocity>0;
    }
};
}
