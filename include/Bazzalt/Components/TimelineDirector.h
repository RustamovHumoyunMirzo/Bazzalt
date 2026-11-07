#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"
#include <cmath>
namespace Bazzalt {
struct TimelineDirector : Component {
    UUID Sequence{};
    bool PlayOnAwake = true, Loop = false, UseUnscaledTime = false, RestoreOnStop = true,
         RestoreOnCompletion = true;
    double Speed = 1;
    bool IsValid() const {
        return std::isfinite(Speed) && Speed > 0 && Speed <= 100;
    }
};
} // namespace Bazzalt
