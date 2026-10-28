#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct WheelTire : Component {
    float LongitudinalPeakSlip=.1f,LongitudinalPeakGrip=1.2f,LongitudinalTailSlip=1,LongitudinalTailGrip=1;
    float LateralPeakAngle=3,LateralPeakGrip=1.2f,LateralTailAngle=20,LateralTailGrip=1;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Positive(LongitudinalPeakSlip)&&Positive(LongitudinalTailSlip)&&LongitudinalTailSlip>LongitudinalPeakSlip&&Nonnegative(LongitudinalPeakGrip)&&Nonnegative(LongitudinalTailGrip)&&Range(LateralPeakAngle,.001f,89)&&Range(LateralTailAngle,LateralPeakAngle+.001f,90)&&Nonnegative(LateralPeakGrip)&&Nonnegative(LateralTailGrip);
    }
};
}
