#pragma once
#include "Bazzalt/Components/Cloth.h"
namespace Bazzalt {
// A collision-enabled soft tube, extending down the local Y axis.
struct Rope : Component {
    float Length=3,Radius=.04f,Mass=1,Compliance=0,BendCompliance=.001f;
    float Damping=.1f,GravityFactor=1,Friction=.5f,Restitution=0,MaxVelocity=100;
    Vec3 Wind{};
    unsigned Segments=24,Sides=6,Iterations=5,Layer=0;
    bool PinStart=true,PinEnd=false,AllowSleeping=true,CastShadows=true;
    UUID MaterialAsset{};
    bool IsValid() const {
        Cloth c;c.Width=Length;c.Height=Radius;c.Mass=Mass;c.Compliance=Compliance;c.BendCompliance=BendCompliance;c.Damping=Damping;c.GravityFactor=GravityFactor;c.Friction=Friction;c.Restitution=Restitution;c.MaxVelocity=MaxVelocity;c.Wind=Wind;c.Iterations=Iterations;c.Layer=Layer;
        return c.IsValid()&&Segments>=2&&Segments<=512&&Sides>=3&&Sides<=16&&(Segments+1)*Sides<=8192;
    }
};
}
