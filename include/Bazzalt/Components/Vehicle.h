#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
namespace Detail::VehicleValidation {
inline bool Positive(float v){return std::isfinite(v)&&v>0;}
inline bool Nonnegative(float v){return std::isfinite(v)&&v>=0;}
inline bool Range(float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
inline bool Finite(Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);}
inline bool Unit(Vec3 v){return Finite(v)&&std::abs(v.LengthSquared()-1)<.001f;}
}
enum class VehicleCollisionMode : std::uint8_t { Ray, SphereCast, CylinderCast };
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct Vehicle : Component {
    Vec3 Up{0,1,0},Forward{0,0,1};
    float MaxPitchRollAngle=180,CollisionRadius=.05f;
    VehicleCollisionMode CollisionTest=VehicleCollisionMode::SphereCast;
    unsigned Layer=0;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Unit(Up)&&Unit(Forward)&&std::abs(Vec3::Dot(Up,Forward))<.001f&&Range(MaxPitchRollAngle,0,180)&&Positive(CollisionRadius)&&int(CollisionTest)<=2&&Layer<32;
    }
};
}
