#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct WheelBrake : Component {
    float MaxTorque=1500,HandbrakeTorque=0;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Nonnegative(MaxTorque)&&Nonnegative(HandbrakeTorque);
    }
};
}
