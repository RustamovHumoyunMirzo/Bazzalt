#pragma once

#include <stdexcept>
#include "Bazzalt/Shader.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {
// Load references a shared material; Instantiate creates a runtime-only independent copy.
class Material final {
public:
    Material() = default;
    [[nodiscard]] static Material Load(UUID id) { Material result; result.m_asset=id; return result; }
    [[nodiscard]] static Material Create(Shader shader) { auto* host=Detail::BoundMaterialServices; return Load(host?host->Clone(shader.GetAssetUUID(),true):UUID{}); }
    [[nodiscard]] UUID GetAssetUUID() const { return m_asset; }
    [[nodiscard]] bool IsValid() const { auto* host=Detail::BoundMaterialServices; return m_asset && host && host->IsValid(m_asset,false); }
    [[nodiscard]] Material Instantiate() const { auto* host=Detail::BoundMaterialServices; return Load(host?host->Clone(m_asset,false):UUID{}); }
    [[nodiscard]] Shader GetShader() const { auto* host=Detail::BoundMaterialServices; return Shader::Load(host?host->GetShader(m_asset):UUID{}); }
    [[nodiscard]] std::vector<ShaderParameter> GetParameters() const { return GetShader().GetParameters(); }
    [[nodiscard]] bool HasParameter(const std::string& name) const { for(const auto& p:GetParameters())if(p.Name==name)return true;return false; }
    void SetFloat(const std::string& name,float value) { Detail::MaterialValue v;v.Numbers[0]=value;Set(name,ShaderParameterType::Float,v); }
    void SetVec2(const std::string& name,Vec2 value) { Detail::MaterialValue v;v.Numbers[0]=value.X;v.Numbers[1]=value.Y;Set(name,ShaderParameterType::Float2,v); }
    void SetVec3(const std::string& name,Vec3 value) { Detail::MaterialValue v;v.Numbers[0]=value.X;v.Numbers[1]=value.Y;v.Numbers[2]=value.Z;Set(name,ShaderParameterType::Float3,v); }
    void SetVec4(const std::string& name,Vec4 value) { Detail::MaterialValue v;v.Numbers[0]=value.X;v.Numbers[1]=value.Y;v.Numbers[2]=value.Z;v.Numbers[3]=value.W;Set(name,ShaderParameterType::Float4,v); }
    void SetColor(const std::string& name,Vec4 value) { SetVec4(name,value); }
    void SetMatrix3(const std::string& name,const Mat3& value) { Detail::MaterialValue v;for(int r=0;r<3;++r)for(int c=0;c<3;++c)v.Numbers[c*3+r]=value(r,c);Set(name,ShaderParameterType::Matrix3,v); }
    void SetMatrix4(const std::string& name,const Mat4& value) { Detail::MaterialValue v;for(int r=0;r<4;++r)for(int c=0;c<4;++c)v.Numbers[c*4+r]=value(r,c);Set(name,ShaderParameterType::Matrix4,v); }
    void SetInteger(const std::string& name,std::int32_t value) { Detail::MaterialValue v;v.Integer=value;Set(name,ShaderParameterType::Integer,v); }
    void SetBoolean(const std::string& name,bool value) { Detail::MaterialValue v;v.Boolean=value;Set(name,ShaderParameterType::Boolean,v); }
    void SetTexture(const std::string& name,UUID value) { Detail::MaterialValue v;v.Texture=value;Set(name,ShaderParameterType::Texture2D,v); }
    [[nodiscard]] float GetFloat(const std::string& name) const { return Get(name,ShaderParameterType::Float).Numbers[0]; }
    [[nodiscard]] Vec2 GetVec2(const std::string& name) const { auto v=Get(name,ShaderParameterType::Float2);return {v.Numbers[0],v.Numbers[1]}; }
    [[nodiscard]] Vec3 GetVec3(const std::string& name) const { auto v=Get(name,ShaderParameterType::Float3);return {v.Numbers[0],v.Numbers[1],v.Numbers[2]}; }
    [[nodiscard]] Vec4 GetVec4(const std::string& name) const { auto v=Get(name,ShaderParameterType::Float4);return {v.Numbers[0],v.Numbers[1],v.Numbers[2],v.Numbers[3]}; }
    [[nodiscard]] Vec4 GetColor(const std::string& name) const { return GetVec4(name); }
    [[nodiscard]] Mat3 GetMatrix3(const std::string& name) const { auto v=Get(name,ShaderParameterType::Matrix3);Mat3 m;for(int r=0;r<3;++r)for(int c=0;c<3;++c)m(r,c)=v.Numbers[c*3+r];return m; }
    [[nodiscard]] Mat4 GetMatrix4(const std::string& name) const { auto v=Get(name,ShaderParameterType::Matrix4);Mat4 m;for(int r=0;r<4;++r)for(int c=0;c<4;++c)m(r,c)=v.Numbers[c*4+r];return m; }
    [[nodiscard]] std::int32_t GetInteger(const std::string& name) const { return Get(name,ShaderParameterType::Integer).Integer; }
    [[nodiscard]] bool GetBoolean(const std::string& name) const { return Get(name,ShaderParameterType::Boolean).Boolean; }
    [[nodiscard]] UUID GetTexture(const std::string& name) const { return Get(name,ShaderParameterType::Texture2D).Texture; }
private:
    void Set(const std::string& name,ShaderParameterType type,const Detail::MaterialValue& value) {
        auto* host=Detail::BoundMaterialServices;
        if(!host || !host->Set(m_asset,name.c_str(),type,&value))throw std::invalid_argument("Material parameter is missing or has an incompatible type: "+name);
    }
    Detail::MaterialValue Get(const std::string& name,ShaderParameterType type) const {
        bool matches=false;for(const auto& p:GetParameters())if(p.Name==name&&p.Type==type){matches=true;break;}
        if(!matches)throw std::invalid_argument("Material parameter is missing or has an incompatible type: "+name);
        Detail::MaterialValue value;auto* host=Detail::BoundMaterialServices;
        if(!host || !host->Get(m_asset,name.c_str(),&value))throw std::invalid_argument("Material parameter is missing: "+name);
        return value;
    }
    UUID m_asset{};
};
} // namespace Bazzalt
