#pragma once

#include <cstddef>
#include <cstdint>

#include "Bazzalt/UUID.h"

namespace Bazzalt::Runtime {

inline constexpr UUID GaussianBlurShaderId{0xba22000000000001ULL, 0x8000000000000001ULL};
inline constexpr UUID VignetteShaderId{0xba22000000000002ULL, 0x8000000000000002ULL};

struct EmbeddedShader {
    const std::uint8_t* Data = nullptr;
    std::size_t Size = 0;
};

[[nodiscard]] EmbeddedShader GetEmbeddedPostProcessShader(UUID id);

} // namespace Bazzalt::Runtime
