#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include <cmath>
#include <cstdint>
namespace Bazzalt {
enum class ColliderShape : std::uint8_t { Box, Sphere, Capsule, Cylinder, Plane };
struct ShapeCollider : Component {
    ColliderShape Shape=ColliderShape::Box;
    Vec3 Size{1},Offset{},Scale{1};
    Quaternion Rotation{};
    float Radius=.5f,Height=2,Friction=.5f,Restitution=0,ConvexRadius=.02f;
    std::uint32_t Layer=0,CollisionGroup=0;
    bool IsTrigger=false;
    [[nodiscard]] bool IsValid() const {
        const auto finite=[](Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);};
        const float length=Rotation.X*Rotation.X+Rotation.Y*Rotation.Y+Rotation.Z*Rotation.Z+Rotation.W*Rotation.W;
        return int(Shape)<=4&&finite(Size)&&Size.X>0&&Size.Y>0&&Size.Z>0&&finite(Offset)&&finite(Scale)&&Scale.X>0&&Scale.Y>0&&Scale.Z>0&&
            std::isfinite(length)&&length>1e-12f&&std::isfinite(Radius)&&Radius>0&&std::isfinite(Height)&&Height>0&&
            std::isfinite(Friction)&&Friction>=0&&std::isfinite(Restitution)&&Restitution>=0&&Restitution<=1&&std::isfinite(ConvexRadius)&&ConvexRadius>=0&&Layer<32;
    }
};
}
