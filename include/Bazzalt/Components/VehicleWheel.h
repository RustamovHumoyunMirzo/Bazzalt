#pragma once
#include "Bazzalt/Components/Vehicle.h"
namespace Bazzalt {
// Vehicle distances are metres, masses kilograms, torque newton-metres, angles degrees.
struct VehicleWheel : Component {
    Vec3 Position{},Up{0,1,0},Forward{0,0,1};
    float Radius=.35f,Width=.2f,Inertia=1,AngularDamping=.2f;
    bool Animate=true;
    [[nodiscard]] bool IsValid() const {
        using namespace Detail::VehicleValidation;
        return Finite(Position)&&Unit(Up)&&Unit(Forward)&&std::abs(Vec3::Dot(Up,Forward))<.99f&&Positive(Radius)&&Positive(Width)&&Positive(Inertia)&&Nonnegative(AngularDamping);
    }
};
}
