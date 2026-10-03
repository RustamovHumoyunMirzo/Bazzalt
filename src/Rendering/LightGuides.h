#pragma once
#include "Rendering/RenderBackend.h"
#include "Bazzalt/Components/Light.h"

namespace Bazzalt::Runtime {
std::vector<RenderBackend::EditorGuide> BuildLightGuides(const Light& light,const Mat4& world);
}
