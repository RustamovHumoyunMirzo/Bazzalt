#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
struct NavigationAgent : Component {
    UUID Surface{};
    float Radius = .5f, Height = 2, Speed = 3.5f, Acceleration = 8, AngularSpeed = 360;
    float StoppingDistance = .1f, BaseOffset = 0, SeparationWeight = 2;
    unsigned AreaMask = 0xffff;
    bool UpdatePosition = true, UpdateRotation = true, Avoidance = true, Stopped = false;
    bool IsValid() const {
        return std::isfinite(Radius) && Radius > 0 && Radius <= 20 && std::isfinite(Height) &&
               Height > 0 && Height <= 50 && std::isfinite(Speed) && Speed >= 0 && Speed <= 1000 &&
               std::isfinite(Acceleration) && Acceleration >= 0 && Acceleration <= 10000 &&
               std::isfinite(AngularSpeed) && AngularSpeed >= 0 && AngularSpeed <= 10000 &&
               std::isfinite(StoppingDistance) && StoppingDistance >= 0 &&
               std::isfinite(BaseOffset) && std::abs(BaseOffset) <= 100 &&
               std::isfinite(SeparationWeight) && SeparationWeight >= 0 &&
               SeparationWeight <= 100 && AreaMask > 0 && AreaMask <= 0xffff;
    }
};
} // namespace Bazzalt
