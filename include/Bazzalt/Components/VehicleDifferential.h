#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleDifferential : Component {
    UUID LeftWheel{},RightWheel{};
    float Ratio=3.42f,Split=.5f,LimitedSlipRatio=1.4f,TorqueShare=1;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Positive(Ratio)&&Range(Split,0,1)&&std::isfinite(LimitedSlipRatio)&&LimitedSlipRatio>1&&Nonnegative(TorqueShare);
    }
};
}
