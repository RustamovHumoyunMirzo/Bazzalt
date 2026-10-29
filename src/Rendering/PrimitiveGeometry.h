#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <array>
#include "Bazzalt/Components/PrimitiveObject.h"

namespace Bazzalt::Runtime {
// Filament stores the normal frame as a quaternion in its TANGENTS attribute.
struct PrimitiveVertex { float Position[3]; float TangentFrame[4]; float UV[2]; };
struct ModelGeometry;
struct PrimitiveGeometry { std::vector<PrimitiveVertex> Vertices; std::vector<std::uint32_t> Indices; Vec3 Extents{};std::shared_ptr<const ModelGeometry> Picking; };
std::shared_ptr<const PrimitiveGeometry> GetCachedPrimitiveGeometry(const PrimitiveObject& primitive);
std::array<std::uint64_t,3> GetPrimitiveCacheStatistics();
PrimitiveGeometry BuildPrimitiveGeometry(const PrimitiveObject& primitive);
}
