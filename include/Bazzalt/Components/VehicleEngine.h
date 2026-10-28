#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleEngine : Component {
    float MaxTorque=500,MinRPM=800,MaxRPM=6500,Inertia=.5f,AngularDamping=.2f;
    float LowTorque=.8f,PeakRPMFraction=.65f,HighTorque=.7f;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Nonnegative(MaxTorque)&&Positive(MinRPM)&&Positive(MaxRPM)&&MaxRPM>MinRPM&&Positive(Inertia)&&Nonnegative(AngularDamping)&&Range(LowTorque,0,1)&&Range(HighTorque,0,1)&&Range(PeakRPMFraction,.01f,.99f);
    }
};
}
