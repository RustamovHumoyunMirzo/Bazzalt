#pragma once
#include "Bazzalt/Export.h"

#include <cstdint>
#include <string>
#include <vector>
#include "Bazzalt/UUID.h"

namespace Bazzalt {
enum class ShaderParameterType : std::uint32_t { Float, Float2, Float3, Float4, Integer, Boolean, Texture2D, Matrix3, Matrix4 };
struct ShaderParameter {
    std::string Name;
    ShaderParameterType Type = ShaderParameterType::Float;
};
namespace Detail {
// Optional host service. No renderer objects or engine-loop operations cross the module ABI.
struct MaterialValue {
    float Numbers[16]{};
    std::int32_t Integer = 0;
    bool Boolean = false;
    UUID Texture{};
};
struct MaterialServices {
    bool (*IsValid)(UUID, bool) = nullptr;
    UUID (*GetShader)(UUID) = nullptr;
    bool (*Parameter)(UUID, std::uint32_t, char*, std::uint32_t, ShaderParameterType*) = nullptr;
    bool (*Get)(UUID, const char*, MaterialValue*) = nullptr;
    bool (*Set)(UUID, const char*, ShaderParameterType, const MaterialValue*) = nullptr;
    UUID (*Clone)(UUID, bool) = nullptr;
};
extern BAZZALT_API MaterialServices* BoundMaterialServices;
}
class Shader final {
public:
    Shader() = default;
    [[nodiscard]] static Shader Load(UUID id) { Shader result; result.m_asset = id; return result; }
    [[nodiscard]] UUID GetAssetUUID() const { return m_asset; }
    [[nodiscard]] bool IsValid() const { auto* host=Detail::BoundMaterialServices; return m_asset && host && host->IsValid(m_asset,true); }
    [[nodiscard]] std::vector<ShaderParameter> GetParameters() const {
        std::vector<ShaderParameter> result;
        auto* host=Detail::BoundMaterialServices;
        if (!host || !m_asset) return result;
        char name[256]; ShaderParameterType type{};
        for(std::uint32_t i=0;host->Parameter(m_asset,i,name,sizeof(name),&type);++i) result.push_back({name,type});
        return result;
    }
private:
    UUID m_asset{};
};
} // namespace Bazzalt
