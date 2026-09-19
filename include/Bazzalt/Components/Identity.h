#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

struct Identity : Component {
    UUID Value{};

    Identity() = default;
    explicit Identity(UUID value) : Value(value) {}
};

} // namespace Bazzalt
