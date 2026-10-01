#pragma once
#include <cstdint>
#include <vector>
#include "Bazzalt/Components/PrimitiveObject.h"

namespace Bazzalt::Runtime {
// Filament stores the normal frame as a quaternion in its TANGENTS attribute.
struct PrimitiveVertex { float Position[3]; float TangentFrame[4]; };
struct PrimitiveGeometry { std::vector<PrimitiveVertex> Vertices; std::vector<std::uint32_t> Indices; Vec3 Extents{}; };
PrimitiveGeometry BuildPrimitiveGeometry(const PrimitiveObject& primitive);
}
