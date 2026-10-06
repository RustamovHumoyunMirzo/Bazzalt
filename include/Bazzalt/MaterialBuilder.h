#pragma once
#include "Bazzalt/Material.h"
#include <functional>
#include <utility>
namespace Bazzalt {
// Defines validated values against a precompiled shader layout. No runtime
// compiler or low-level renderer object leaks through this public API.
class MaterialBuilder final {
public:
    explicit MaterialBuilder(Shader shader = Shader::Builtin()) : m_shader(shader) {}
    MaterialBuilder& SetShader(Shader shader) { m_shader=shader; return *this; }
    MaterialBuilder& SetRenderState(MaterialRenderState state) { m_state=state; m_hasState=true; return *this; }
#define BAZZALT_MATERIAL_BUILDER_SET(Name,Type) \
    MaterialBuilder& Name(std::string name,Type value) { \
        m_setters.emplace_back([name=std::move(name),value](Material& material){ material.Name(name,value); }); return *this; }
    BAZZALT_MATERIAL_BUILDER_SET(SetFloat,float)
    BAZZALT_MATERIAL_BUILDER_SET(SetVec2,Vec2)
    BAZZALT_MATERIAL_BUILDER_SET(SetVec3,Vec3)
    BAZZALT_MATERIAL_BUILDER_SET(SetVec4,Vec4)
    BAZZALT_MATERIAL_BUILDER_SET(SetColor,Vec4)
    BAZZALT_MATERIAL_BUILDER_SET(SetMatrix3,Mat3)
    BAZZALT_MATERIAL_BUILDER_SET(SetMatrix4,Mat4)
    BAZZALT_MATERIAL_BUILDER_SET(SetInteger,std::int32_t)
    BAZZALT_MATERIAL_BUILDER_SET(SetBoolean,bool)
    BAZZALT_MATERIAL_BUILDER_SET(SetTexture,UUID)
#undef BAZZALT_MATERIAL_BUILDER_SET
    [[nodiscard]] Material Build() const {
        Material material=Material::Create(m_shader);
        if(!material.IsValid())throw std::invalid_argument("MaterialBuilder requires a valid shader");
        try{for(const auto& setter:m_setters)setter(material);if(m_hasState)material.SetRenderState(m_state);}
        catch(...){material.Destroy();throw;}
        return material;
    }
    void Clear() { m_setters.clear();m_hasState=false; }
private:
    Shader m_shader;
    MaterialRenderState m_state{};
    bool m_hasState=false;
    std::vector<std::function<void(Material&)>> m_setters;
};
}
