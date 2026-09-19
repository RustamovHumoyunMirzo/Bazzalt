#pragma once

#include <string>
#include <utility>

#include "Bazzalt/Component.h"

namespace Bazzalt {

struct Name : Component {
    std::string Value;

    Name() = default;
    explicit Name(std::string value)
        : Value(std::move(value)) {}
};

} // namespace Bazzalt
