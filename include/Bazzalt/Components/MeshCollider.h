#pragma once
#include "Bazzalt/Components/ShapeCollider.h"
#include "Bazzalt/UUID.h"
namespace Bazzalt {
struct MeshCollider : Component {
    UUID MeshAsset{};
    Vec3 Offset{},Scale{1};
    Quaternion Rotation{};
    float Friction=.5f,Restitution=0,ConvexRadius=.02f;
    std::uint32_t Layer=0,CollisionGroup=0;
    bool Convex=true,IsTrigger=false,DoubleSided=true,Sanitize=true;
    [[nodiscard]] bool IsValid() const {
        ShapeCollider v;v.Offset=Offset;v.Scale=Scale;v.Rotation=Rotation;v.Friction=Friction;v.Restitution=Restitution;v.ConvexRadius=ConvexRadius;v.Layer=Layer;
        return v.IsValid();
    }
};
}
