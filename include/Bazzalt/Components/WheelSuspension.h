#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct WheelSuspension : Component {
    Vec3 Direction{0,-1,0},ForcePoint{};
    float MinLength=.15f,MaxLength=.5f,PreloadLength=0,Frequency=2,Damping=.7f;
    bool UseForcePoint=false;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Unit(Direction)&&Finite(ForcePoint)&&Nonnegative(MinLength)&&Positive(MaxLength)&&MaxLength>MinLength&&Nonnegative(PreloadLength)&&Positive(Frequency)&&Nonnegative(Damping);
    }
};
}
