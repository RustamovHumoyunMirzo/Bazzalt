#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
enum class JointType : unsigned char { Distance, Hinge, Slider, BallAndSocket, Fixed };
enum class JointMotorMode : unsigned char { Off, Velocity, Position };
// Anchors are entity-local; an unassigned ConnectedBody anchors to world space.
// Angles are radians, lengths are world units. Zero break thresholds never break.
struct Joint : Component {
    JointType Type=JointType::Fixed;
    UUID ConnectedBody{};
    Vec3 Anchor{},ConnectedAnchor{},Axis{0,1,0};
    bool AutoConfigureConnectedAnchor=true,EnableCollision=false;
    float BreakForce=0,BreakTorque=0;
    float MinDistance=0,MaxDistance=1,MinLimit=-3.14159265f,MaxLimit=3.14159265f;
    float SpringFrequency=0,SpringDamping=1,Friction=0;
    JointMotorMode Motor=JointMotorMode::Off;
    float MotorVelocity=0,MotorTarget=0,MotorForce=1000,MotorFrequency=2,MotorDamping=1;
    bool IsValid() const {
        auto finite=[](Vec3 v){return std::isfinite(v.X)&&std::isfinite(v.Y)&&std::isfinite(v.Z);};
        for(float v:{BreakForce,BreakTorque,MinDistance,MaxDistance,MinLimit,MaxLimit,SpringFrequency,SpringDamping,Friction,MotorVelocity,MotorTarget,MotorForce,MotorFrequency,MotorDamping})if(!std::isfinite(v))return false;
        return int(Type)<=4&&int(Motor)<=2&&finite(Anchor)&&finite(ConnectedAnchor)&&finite(Axis)&&Axis.LengthSquared()>1e-10f&&BreakForce>=0&&BreakTorque>=0&&MinDistance>=0&&MaxDistance>=MinDistance&&MinLimit<=0&&MaxLimit>=0&&MinLimit<=MaxLimit&&SpringFrequency>=0&&SpringDamping>=0&&Friction>=0&&MotorForce>=0&&MotorFrequency>0&&MotorDamping>=0&&(Type!=JointType::Hinge||(MinLimit>=-3.14159265f&&MaxLimit<=3.14159265f));
    }
};
struct DistanceJoint : Joint { DistanceJoint(){Type=JointType::Distance;} };
struct HingeJoint : Joint { HingeJoint(){Type=JointType::Hinge;} };
struct SliderJoint : Joint { SliderJoint(){Type=JointType::Slider;MinLimit=-1;MaxLimit=1;Axis={1,0,0};} };
struct BallAndSocketJoint : Joint { BallAndSocketJoint(){Type=JointType::BallAndSocket;} };
struct FixedJoint : Joint { FixedJoint(){Type=JointType::Fixed;} };
}
