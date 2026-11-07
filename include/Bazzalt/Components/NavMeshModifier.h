#pragma once
#include "Bazzalt/Component.h"
namespace Bazzalt {
// Applies to geometry on this entity. Area 0 is unwalkable; areas 1..16 map to mask bits.
struct NavMeshModifier : Component {
    unsigned Area = 1;
    bool Ignore = false;
    bool IsValid() const {
        return Area <= 16;
    }
};
} // namespace Bazzalt
