#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "Bazzalt/Entity.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

struct SceneRay {
    Vec3 Origin{};
    Vec3 Direction{0, 0, -1};
};

struct SceneQueryOptions {
    std::uint32_t LayerMask = 0xffffffffu;
    float MaxDistance = std::numeric_limits<float>::infinity();
    bool IncludeDisabled = false;
    std::function<bool(Entity)> Filter;
};

struct SceneQueryHit {
    Entity Target{};
    Vec3 Point{};
    Vec3 Normal{};
    float Distance = 0.0f;
    bool StartedInside = false;

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(Target); }
};

} // namespace Bazzalt
