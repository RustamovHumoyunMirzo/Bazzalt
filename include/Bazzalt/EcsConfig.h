#pragma once
#include <entt/core/type_info.hpp>

// Compiler-independent IDs for engine components shared with gameplay DLLs.
// User-defined types use EnTT's ordinary type hash within the script toolchain.
namespace Bazzalt {
struct Camera; struct GaussianBlur; struct Hierarchy; struct Identity;
struct Light; struct Mesh; struct ModelInstance; struct ModelNode; struct Name;
struct PrimitiveObject; struct SceneQueryBounds; struct ScriptComponents;
struct Transform; struct Vignette; struct UnresolvedComponents;
}
#define BAZZALT_COMPONENT_ID(Type) \
    template<> struct type_hash<Bazzalt::Type> { \
        static constexpr id_type value() noexcept { return hashed_string::value("Bazzalt::" #Type); } \
        constexpr operator id_type() const noexcept { return value(); } \
    };
namespace entt {
BAZZALT_COMPONENT_ID(Camera)
BAZZALT_COMPONENT_ID(GaussianBlur)
BAZZALT_COMPONENT_ID(Hierarchy)
BAZZALT_COMPONENT_ID(Identity)
BAZZALT_COMPONENT_ID(Light)
BAZZALT_COMPONENT_ID(Mesh)
BAZZALT_COMPONENT_ID(ModelInstance)
BAZZALT_COMPONENT_ID(ModelNode)
BAZZALT_COMPONENT_ID(Name)
BAZZALT_COMPONENT_ID(PrimitiveObject)
BAZZALT_COMPONENT_ID(SceneQueryBounds)
BAZZALT_COMPONENT_ID(ScriptComponents)
BAZZALT_COMPONENT_ID(Transform)
BAZZALT_COMPONENT_ID(Vignette)
BAZZALT_COMPONENT_ID(UnresolvedComponents)
}
#undef BAZZALT_COMPONENT_ID
