#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleAerodynamics : Component {
    float AirDensity=1.225f,FrontalArea=2.2f,DragCoefficient=.3f,DownforceCoefficient=0;
    Vec3 CenterOfPressure{};
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Nonnegative(AirDensity)&&Nonnegative(FrontalArea)&&Nonnegative(DragCoefficient)&&Nonnegative(DownforceCoefficient)&&Finite(CenterOfPressure);
    }
};
}
