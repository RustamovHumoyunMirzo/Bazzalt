#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleAntiRollBar : Component {
    UUID LeftWheel{},RightWheel{};
    float Stiffness=1000;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Nonnegative(Stiffness);
    }
};
}
