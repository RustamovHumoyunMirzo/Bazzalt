#pragma once
#include <cstdint>
#include <vector>
#include <optional>
namespace Bazzalt::Runtime {
// Private presentation transfer. No renderer or window-system types cross here.
struct ViewportFrame {std::uint64_t Sequence=0;std::uint32_t Width=0,Height=0;std::vector<std::uint8_t> Pixels;};
}
