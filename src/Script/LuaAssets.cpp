#include "Script/LuaBindings.h"
#include "Bazzalt/MaterialBuilder.h"
namespace Bazzalt::Runtime {
void BindLuaAssets(sol::table api){
    auto parameterTypes=api.create_named("PostProcessParameterType");
    parameterTypes["Float"]=PostProcessParameterType::Float;parameterTypes["Float2"]=PostProcessParameterType::Float2;parameterTypes["Float3"]=PostProcessParameterType::Float3;parameterTypes["Float4"]=PostProcessParameterType::Float4;
    parameterTypes["Integer"]=PostProcessParameterType::Integer;parameterTypes["Boolean"]=PostProcessParameterType::Boolean;parameterTypes["Texture"]=PostProcessParameterType::Texture;
    {auto type=api.new_usertype<AssetInfo>("AssetInfo",sol::constructors<AssetInfo()>());
     type["Id"]=&AssetInfo::Id;
     type["Importer"]=&AssetInfo::Importer;
     type["ImporterVersion"]=&AssetInfo::ImporterVersion;
     type["State"]=&AssetInfo::State;
     type["LastError"]=&AssetInfo::LastError;
    }
    {auto type=api.new_usertype<ModelAssetNode>("ModelAssetNode",sol::constructors<ModelAssetNode()>());
     type["Name"]=&ModelAssetNode::Name;
     type["StablePath"]=&ModelAssetNode::StablePath;
     type["SourceIndex"]=&ModelAssetNode::SourceIndex;
     type["MeshIndex"]=&ModelAssetNode::MeshIndex;
     type["Position"]=&ModelAssetNode::Position;
     type["Rotation"]=&ModelAssetNode::Rotation;
     type["Scale"]=&ModelAssetNode::Scale;
     type["Children"]=&ModelAssetNode::Children;
     type["MaterialNames"]=&ModelAssetNode::MaterialNames;
    }
    {auto type=api.new_usertype<ModelAsset>("ModelAsset",sol::constructors<ModelAsset()>());
     type["Id"]=&ModelAsset::Id;
     type["Name"]=&ModelAsset::Name;
     type["Nodes"]=&ModelAsset::Nodes;
     type["Roots"]=&ModelAsset::Roots;
    }
    {auto type=api.new_usertype<SceneEnvironment>("SceneEnvironment",sol::constructors<SceneEnvironment()>());
     type["Mode"]=&SceneEnvironment::Mode;
     type["SourceAsset"]=&SceneEnvironment::SourceAsset;
     type["MaterialAsset"]=&SceneEnvironment::MaterialAsset;
     type["ClearColor"]=&SceneEnvironment::ClearColor;
     type["Rotation"]=&SceneEnvironment::Rotation;
     type["Intensity"]=&SceneEnvironment::Intensity;
     type["ImageBasedLighting"]=&SceneEnvironment::ImageBasedLighting;
     type["SkyboxVisible"]=&SceneEnvironment::SkyboxVisible;
     type["ShowSun"]=&SceneEnvironment::ShowSun;
    }
    {auto type=api.new_usertype<ShaderParameter>("ShaderParameter",sol::constructors<ShaderParameter()>());
     type["Name"]=&ShaderParameter::Name;
     type["Type"]=&ShaderParameter::Type;
    }
    {auto type=api.new_usertype<PostProcessParameter>("PostProcessParameter",sol::constructors<PostProcessParameter()>());
     type["Name"]=&PostProcessParameter::Name;
     type["Type"]=&PostProcessParameter::Type;
     type["Value"]=&PostProcessParameter::Value;
     type["IntegerValue"]=&PostProcessParameter::IntegerValue;
     type["BooleanValue"]=&PostProcessParameter::BooleanValue;
     type["TextureAsset"]=&PostProcessParameter::TextureAsset;
    }
    {auto type=api.new_usertype<CustomPostProcessEffect>("CustomPostProcessEffect",sol::constructors<CustomPostProcessEffect()>());
     type["ShaderAsset"]=&CustomPostProcessEffect::ShaderAsset;
     type["Name"]=&CustomPostProcessEffect::Name;
     type["Enabled"]=&CustomPostProcessEffect::Enabled;
     type["Order"]=&CustomPostProcessEffect::Order;
     type["Parameters"]=&CustomPostProcessEffect::Parameters;
    }
    sol::usertype<AssetInfo> assetInfo=api["AssetInfo"];
    assetInfo["SourcePath"]=sol::property([](const AssetInfo& value){auto text=value.SourcePath.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());});
    assetInfo["MetaPath"]=sol::property([](const AssetInfo& value){auto text=value.MetaPath.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());});
    assetInfo["CachePath"]=sol::property([](const AssetInfo& value){auto text=value.CachePath.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());});
    sol::usertype<SceneEnvironment> environment=api["SceneEnvironment"];environment["IsValid"]=&SceneEnvironment::IsValid;
    sol::usertype<ModelAssetNode> node=api["ModelAssetNode"];node["HasMesh"]=&ModelAssetNode::HasMesh;node["NoMesh"]=sol::var(ModelAssetNode::NoMesh);
    auto shader=api.new_usertype<Shader>("Shader",sol::constructors<Shader()>());shader["Load"]=&Shader::Load;shader["GetAssetUUID"]=&Shader::GetAssetUUID;shader["IsValid"]=&Shader::IsValid;shader["GetParameters"]=[](Shader value){return sol::as_table(value.GetParameters());};
    auto material=api.new_usertype<Material>("Material",sol::constructors<Material()>());
    material["Load"]=&Material::Load;
    material["Create"]=sol::overload([](){return Material::Create();},[](Shader shader){return Material::Create(shader);},[](ShaderPreset preset){return Material::Create(preset);});
    auto presets=api.create_named("ShaderPreset");
    presets["StandardLit"]=ShaderPreset::StandardLit;presets["Unlit"]=ShaderPreset::Unlit;
    presets["StandardLitTransparent"]=ShaderPreset::StandardLitTransparent;presets["UnlitTransparent"]=ShaderPreset::UnlitTransparent;
    shader["Builtin"]=[](sol::optional<ShaderPreset> preset){return Shader::Builtin(preset.value_or(ShaderPreset::StandardLit));};
    auto culling=api.create_named("MaterialCulling");culling["None"]=MaterialCulling::None;culling["Front"]=MaterialCulling::Front;culling["Back"]=MaterialCulling::Back;culling["FrontAndBack"]=MaterialCulling::FrontAndBack;
    auto depth=api.create_named("MaterialDepthFunction");
#define BAZZALT_LUA_DEPTH(Name) depth[#Name]=MaterialDepthFunction::Name;
    BAZZALT_LUA_DEPTH(Less) BAZZALT_LUA_DEPTH(LessEqual) BAZZALT_LUA_DEPTH(Equal) BAZZALT_LUA_DEPTH(Greater) BAZZALT_LUA_DEPTH(GreaterEqual) BAZZALT_LUA_DEPTH(Always) BAZZALT_LUA_DEPTH(Never) BAZZALT_LUA_DEPTH(NotEqual)
#undef BAZZALT_LUA_DEPTH
    auto state=api.new_usertype<MaterialRenderState>("MaterialRenderState",sol::constructors<MaterialRenderState()>());
#define BAZZALT_LUA_STATE(Name) state[#Name]=&MaterialRenderState::Name;
    BAZZALT_LUA_STATE(Override) BAZZALT_LUA_STATE(DoubleSided) BAZZALT_LUA_STATE(DepthTest) BAZZALT_LUA_STATE(DepthWrite) BAZZALT_LUA_STATE(ColorWrite) BAZZALT_LUA_STATE(Culling) BAZZALT_LUA_STATE(DepthFunction)
#undef BAZZALT_LUA_STATE
    material["IsRuntime"]=&Material::IsRuntime;material["Destroy"]=&Material::Destroy;
    material["SetShader"]=[](Material& m,Shader s,sol::optional<bool> preserve){m.SetShader(s,preserve.value_or(true));};
    material["CopyPropertiesFrom"]=[](Material& m,Material s,sol::optional<bool> state){m.CopyPropertiesFrom(s,state.value_or(true));};
    material["ResetParameter"]=&Material::ResetParameter;material["ResetProperties"]=&Material::ResetProperties;material["HasOverride"]=&Material::HasOverride;
    material["GetRenderState"]=&Material::GetRenderState;material["SetRenderState"]=&Material::SetRenderState;material["ResetRenderState"]=&Material::ResetRenderState;
    material["AllSlots"]=sol::var(-1);
    material["ApplyTo"]=[](const Material& m,Entity entity,sol::optional<std::int64_t> slot,sol::optional<bool> children){auto index=slot.value_or(-1);if(index < -1 || index>=4096)throw std::out_of_range("Invalid material slot");return m.ApplyTo(entity,index==-1?Material::AllSlots:static_cast<std::size_t>(index),children.value_or(false));};
    auto builder=api.new_usertype<MaterialBuilder>("MaterialBuilder",sol::constructors<MaterialBuilder(),MaterialBuilder(Shader)>());
#define BAZZALT_LUA_BUILDER(Name) builder[#Name]=&MaterialBuilder::Name;
    BAZZALT_LUA_BUILDER(SetShader) BAZZALT_LUA_BUILDER(SetRenderState) BAZZALT_LUA_BUILDER(SetFloat) BAZZALT_LUA_BUILDER(SetVec2) BAZZALT_LUA_BUILDER(SetVec3) BAZZALT_LUA_BUILDER(SetVec4) BAZZALT_LUA_BUILDER(SetColor) BAZZALT_LUA_BUILDER(SetMatrix3) BAZZALT_LUA_BUILDER(SetMatrix4) BAZZALT_LUA_BUILDER(SetInteger) BAZZALT_LUA_BUILDER(SetBoolean) BAZZALT_LUA_BUILDER(SetTexture) BAZZALT_LUA_BUILDER(Build) BAZZALT_LUA_BUILDER(Clear)
#undef BAZZALT_LUA_BUILDER
    material["GetAssetUUID"]=&Material::GetAssetUUID;
    material["IsValid"]=&Material::IsValid;
    material["Instantiate"]=&Material::Instantiate;
    material["GetShader"]=&Material::GetShader;
    material["HasParameter"]=&Material::HasParameter;
    material["SetFloat"]=&Material::SetFloat;
    material["SetVec2"]=&Material::SetVec2;
    material["SetVec3"]=&Material::SetVec3;
    material["SetVec4"]=&Material::SetVec4;
    material["SetColor"]=&Material::SetColor;
    material["SetMatrix3"]=&Material::SetMatrix3;
    material["SetMatrix4"]=&Material::SetMatrix4;
    material["SetInteger"]=&Material::SetInteger;
    material["SetBoolean"]=&Material::SetBoolean;
    material["SetTexture"]=&Material::SetTexture;
    material["GetFloat"]=&Material::GetFloat;
    material["GetVec2"]=&Material::GetVec2;
    material["GetVec3"]=&Material::GetVec3;
    material["GetVec4"]=&Material::GetVec4;
    material["GetColor"]=&Material::GetColor;
    material["GetMatrix3"]=&Material::GetMatrix3;
    material["GetMatrix4"]=&Material::GetMatrix4;
    material["GetInteger"]=&Material::GetInteger;
    material["GetBoolean"]=&Material::GetBoolean;
    material["GetTexture"]=&Material::GetTexture;
    material["GetParameters"]=[](Material value){return sol::as_table(value.GetParameters());};
    auto assets=api.create_named("AssetManager");
    assets["GetAsset"]=sol::overload([](UUID id){return AssetManager::GetAsset(id);},[](const std::string& path){return AssetManager::GetAsset(std::filesystem::u8path(path));});
    assets["IsAssetReady"]=&AssetManager::IsAssetReady;assets["LoadModel"]=&AssetManager::LoadModel;
    auto stack=api.new_usertype<PostProcessingStack>("PostProcessingStack",sol::constructors<PostProcessingStack()>());
    stack["AddEffect"]=[](PostProcessingStack& value,UUID asset,sol::optional<std::string> name){return CustomPostProcessEffect(value.AddEffect(asset,name.value_or("")));};
    stack["GetEffects"]=[](PostProcessingStack& value){return sol::as_table(value.GetEffects());};
    stack["SetEffect"]=[](PostProcessingStack& value,std::size_t index,CustomPostProcessEffect effect){value.GetEffects().at(index)=std::move(effect);};
    stack["RemoveEffect"]=&PostProcessingStack::RemoveEffect;stack["Clear"]=&PostProcessingStack::Clear;stack["IsEmpty"]=&PostProcessingStack::IsEmpty;
    sol::usertype<PostProcessParameter> parameter=api["PostProcessParameter"];
    parameter["Float"]=&PostProcessParameter::Float;
    parameter["Float2"]=&PostProcessParameter::Float2;
    parameter["Float3"]=&PostProcessParameter::Float3;
    parameter["Float4"]=&PostProcessParameter::Float4;
    parameter["Integer"]=&PostProcessParameter::Integer;
    parameter["Boolean"]=&PostProcessParameter::Boolean;
    parameter["Texture"]=&PostProcessParameter::Texture;
}
}
