#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

enum class PostProcessParameterType { Float, Float2, Float3, Float4, Integer, Boolean, Texture };

struct PostProcessParameter {
    std::string Name;
    PostProcessParameterType Type = PostProcessParameterType::Float;
    Vec4 Value{};
    std::int32_t IntegerValue = 0;
    bool BooleanValue = false;
    UUID TextureAsset{};

    [[nodiscard]] static PostProcessParameter Float(std::string name, float value) {
        return {std::move(name), PostProcessParameterType::Float, {value, 0, 0, 0}};
    }
    [[nodiscard]] static PostProcessParameter Float2(std::string name, Vec2 value) {
        return {std::move(name), PostProcessParameterType::Float2, {value.X, value.Y, 0, 0}};
    }
    [[nodiscard]] static PostProcessParameter Float3(std::string name, Vec3 value) {
        return {std::move(name), PostProcessParameterType::Float3,
                {value.X, value.Y, value.Z, 0}};
    }
    [[nodiscard]] static PostProcessParameter Float4(std::string name, Vec4 value) {
        return {std::move(name), PostProcessParameterType::Float4, value};
    }
    [[nodiscard]] static PostProcessParameter Integer(std::string name, std::int32_t value) {
        PostProcessParameter result; result.Name=std::move(name); result.Type=PostProcessParameterType::Integer;
        result.IntegerValue=value; return result;
    }
    [[nodiscard]] static PostProcessParameter Boolean(std::string name, bool value) {
        PostProcessParameter result; result.Name=std::move(name); result.Type=PostProcessParameterType::Boolean;
        result.BooleanValue=value; return result;
    }
    [[nodiscard]] static PostProcessParameter Texture(std::string name, UUID asset) {
        PostProcessParameter result; result.Name=std::move(name); result.Type=PostProcessParameterType::Texture;
        result.TextureAsset=asset; return result;
    }
};

struct CustomPostProcessEffect {
    UUID ShaderAsset{};
    std::string Name;
    bool Enabled = true;
    int Order = 0;
    std::vector<PostProcessParameter> Parameters;

    PostProcessParameter& SetParameter(PostProcessParameter parameter) {
        if (parameter.Name.empty()) throw std::invalid_argument("Post-process parameter name cannot be empty");
        for (auto& existing : Parameters) if (existing.Name == parameter.Name) {
            existing = std::move(parameter); return existing;
        }
        Parameters.push_back(std::move(parameter)); return Parameters.back();
    }
};

class PostProcessingStack final {
public:
    CustomPostProcessEffect& AddEffect(UUID shaderAsset, std::string name = {}) {
        if (!shaderAsset) throw std::invalid_argument("Post-process shader asset UUID cannot be zero");
        m_effects.push_back({shaderAsset, std::move(name)});
        return m_effects.back();
    }
    void RemoveEffect(std::size_t index) {
        if (index >= m_effects.size()) throw std::out_of_range("Post-process effect index is invalid");
        m_effects.erase(m_effects.begin() + static_cast<std::ptrdiff_t>(index));
    }
    void Clear() { m_effects.clear(); }
    [[nodiscard]] std::vector<CustomPostProcessEffect>& GetEffects() { return m_effects; }
    [[nodiscard]] const std::vector<CustomPostProcessEffect>& GetEffects() const { return m_effects; }
    [[nodiscard]] bool IsEmpty() const { return m_effects.empty(); }
private:
    std::vector<CustomPostProcessEffect> m_effects;
};

} // namespace Bazzalt
