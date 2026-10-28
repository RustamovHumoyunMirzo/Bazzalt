#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct WheelSteering : Component {
    Vec3 Axis{0,1,0};
    float MaxAngle=35;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Unit(Axis)&&Range(MaxAngle,0,85);
    }
};
}
