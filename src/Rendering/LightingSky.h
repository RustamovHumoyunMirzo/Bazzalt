#pragma once
#include <cstdint>
#include "Bazzalt/Components/ReflectionProbe.h"
#include "Bazzalt/SceneEnvironment.h"
namespace Bazzalt::Runtime {
ReflectionProbe DecodeLightingSky(const std::vector<std::uint8_t>& bytes,float intensity);
ReflectionProbe ReadLightingSky(const SceneEnvironment& environment);
}
