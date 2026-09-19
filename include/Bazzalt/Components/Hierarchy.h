#pragma once

#include <vector>

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct Hierarchy : Component {
    UUID Parent{};
    std::vector<UUID> Children;
};

} // namespace Bazzalt
