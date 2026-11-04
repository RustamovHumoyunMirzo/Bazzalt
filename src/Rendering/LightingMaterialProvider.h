#pragma once
#include <functional>
namespace filament {class Engine;class MaterialInstance;namespace gltfio {class MaterialProvider;}}
namespace Bazzalt::Runtime {
// Adapted from the pinned Apache-2.0 gltfio JIT provider; original PBR and texture bindings retained.
filament::gltfio::MaterialProvider* CreateLightingMaterialProvider(filament::Engine*,std::function<void(filament::MaterialInstance*)> bind);
}
