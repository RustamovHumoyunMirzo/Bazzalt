#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include <optional>
#include <cmath>
#include <cstdint>
namespace Bazzalt {
enum class BodyMotionType : std::uint8_t { Static, Kinematic, Dynamic };
enum class CollisionDetectionMode : std::uint8_t { Discrete, Continuous };
enum class BodyInterpolation : std::uint8_t { None, Interpolate };
enum class BodyConstraints : std::uint8_t { None=0, PositionX=1,PositionY=2,PositionZ=4,RotationX=8,RotationY=16,RotationZ=32,All=63 };
[[nodiscard]] constexpr BodyConstraints operator|(BodyConstraints a,BodyConstraints b){return BodyConstraints(std::uint8_t(a)|std::uint8_t(b));}
[[nodiscard]] constexpr BodyConstraints operator&(BodyConstraints a,BodyConstraints b){return BodyConstraints(std::uint8_t(a)&std::uint8_t(b));}
[[nodiscard]] constexpr BodyConstraints operator^(BodyConstraints a,BodyConstraints b){return BodyConstraints(std::uint8_t(a)^std::uint8_t(b));}
[[nodiscard]] constexpr BodyConstraints operator~(BodyConstraints a){return BodyConstraints(std::uint8_t(BodyConstraints::All)^std::uint8_t(a));}
constexpr BodyConstraints& operator|=(BodyConstraints& a,BodyConstraints b){return a=a|b;}
constexpr BodyConstraints& operator&=(BodyConstraints& a,BodyConstraints b){return a=a&b;}
struct Rigidbody : Component {
    BodyMotionType MotionType=BodyMotionType::Dynamic;
    CollisionDetectionMode CollisionDetection=CollisionDetectionMode::Discrete;
    BodyInterpolation Interpolation=BodyInterpolation::Interpolate;
    BodyConstraints Constraints=BodyConstraints::None;
    float Mass=1,InertiaMultiplier=1,LinearDamping=.05f,AngularDamping=.05f;
    float MaxLinearVelocity=500,MaxAngularVelocity=47.12389f,GravityFactor=1;
    Vec3 CenterOfMassOffset{},LinearVelocity{},AngularVelocity{};
    std::optional<Vec3> Gravity;
    bool AllowSleeping=true,StartAwake=true,UseManifoldReduction=true;
    bool ApplyGyroscopicForce=false,CollideKinematicVsNonDynamic=false,EnhancedInternalEdgeRemoval=false;
    std::uint32_t VelocityIterations=0,PositionIterations=0;
    [[nodiscard]] bool IsValid() const {
        const auto finite=[](Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);};
        return int(MotionType)<=2&&int(CollisionDetection)<=1&&int(Interpolation)<=1&&int(Constraints)<=63&&
            std::isfinite(Mass)&&Mass>0&&std::isfinite(InertiaMultiplier)&&InertiaMultiplier>0&&
            std::isfinite(LinearDamping)&&LinearDamping>=0&&std::isfinite(AngularDamping)&&AngularDamping>=0&&
            std::isfinite(MaxLinearVelocity)&&MaxLinearVelocity>0&&std::isfinite(MaxAngularVelocity)&&MaxAngularVelocity>0&&
            std::isfinite(GravityFactor)&&finite(CenterOfMassOffset)&&finite(LinearVelocity)&&finite(AngularVelocity)&&(!Gravity||finite(*Gravity))&&VelocityIterations<=255&&PositionIterations<=255;
    }
};
}
