#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleTransmission : Component {
    bool Automatic=true;
    unsigned GearCount=6;
    float Gear1=3.2f,Gear2=2.1f,Gear3=1.5f,Gear4=1.15f,Gear5=.9f,Gear6=.72f,Gear7=.6f,Gear8=.5f,Gear9=.43f,Gear10=.37f,ReverseRatio=-3;
    float SwitchTime=.3f,ClutchReleaseTime=.2f,SwitchLatency=.3f,ShiftUpRPM=5500,ShiftDownRPM=2000,ClutchStrength=10;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return GearCount>=1&&GearCount<=10&&Positive(Gear1)&&Positive(Gear2)&&Positive(Gear3)&&Positive(Gear4)&&Positive(Gear5)&&Positive(Gear6)&&Positive(Gear7)&&Positive(Gear8)&&Positive(Gear9)&&Positive(Gear10)&&std::isfinite(ReverseRatio)&&ReverseRatio<0&&Nonnegative(SwitchTime)&&Nonnegative(ClutchReleaseTime)&&Nonnegative(SwitchLatency)&&Positive(ShiftDownRPM)&&ShiftUpRPM>ShiftDownRPM&&std::isfinite(ShiftUpRPM)&&Positive(ClutchStrength);
    }
};
}
