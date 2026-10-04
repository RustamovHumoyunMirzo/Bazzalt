#include "Rendering/RenderAssets.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <cstring>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/MaterialEnums.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filameshio/MeshReader.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>
#include <utils/Path.h>

#include "Bazzalt/AssetManager.h"
extern "C" float* stbi_loadf_from_memory(const unsigned char*,int,int*,int*,int*,int);
extern "C" void stbi_image_free(void*);
#include "Runtime/MaterialLibrary.h"
#include "Rendering/BuiltinPostProcess.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/ModelGeometry.h"
#include "editor_unlit_filamat.h"
#include "editor_lighting_only_filamat.h"
#include "editor_overdraw_filamat.h"
#include "editor_wireframe_filamat.h"
#include "primitive_lit_filamat.h"

namespace Bazzalt::Runtime {
namespace {

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const auto size = stream.tellg();
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return stream ? std::move(bytes) : std::vector<std::uint8_t>{};
}

std::string LowerExtension(const std::filesystem::path& path) {
    const auto bytes=path.extension().u8string();std::string result(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return result;
}

std::string PathUtf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<std::uint8_t> ReadGltfResource(const std::filesystem::path& source, std::string_view uri) {
    const auto relative=std::filesystem::u8path(uri).lexically_normal();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        *relative.begin()==".." || uri.find(':')!=std::string_view::npos) return {};
    const auto base=std::filesystem::weakly_canonical(source.parent_path());
    const auto path=std::filesystem::weakly_canonical(base/relative);
    const auto inside=path.lexically_relative(base);
    if (inside.empty() || inside.is_absolute() || *inside.begin()=="..") return {};
    return ReadBytes(path);
}

// Desktop cgltf ignores ResourceLoader's URI cache for geometry buffers and
// uses narrow fopen. Embed external buffers in memory, leaving cached files
// untouched. Images are supplied separately through addResourceData.
bool EmbedGltfBuffers(std::vector<std::uint8_t>& bytes, const std::filesystem::path& source) {
    const bool binary=LowerExtension(source)==".glb";
    auto word=[&](std::size_t offset) { std::uint32_t value=0;std::memcpy(&value,bytes.data()+offset,4);return value; };
    std::size_t start=0,length=bytes.size(),tail=bytes.size();
    if (binary) {
        if(bytes.size()<20 || word(0)!=0x46546c67 || word(4)!=2 || word(12)>bytes.size()-20 || word(16)!=0x4e4f534a) return false;
        start=20;length=word(12);tail=start+length;
    }
    auto tree=ryml::parse_in_arena(ryml::csubstr(reinterpret_cast<const char*>(bytes.data()+start),length));
    auto root=tree.rootref();
    bool changed=false;
    // Filament partitions its entity list into renderable / non-renderable
    // nodes. Never use that list's offset as a glTF source node index.
    if(root.has_child("nodes")) {
        std::size_t index=0;
        for(auto node:root["nodes"].children()) {
            const auto name="__bazzalt_node_"+std::to_string(index++);
            node["name"] << name;
        }
        changed=true;
    }
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(root.has_child("buffers"))for(auto buffer:root["buffers"].children()) {
        if(!buffer.has_child("uri"))continue;
        auto field=buffer["uri"];auto value=field.val();std::string uri(value.str,value.len);
        if(std::string_view(uri).starts_with("data:"))continue;
        auto data=ReadGltfResource(source,uri);if(data.empty())return false;
        std::string encoded="data:application/octet-stream;base64,";
        encoded.reserve(encoded.size()+((data.size()+2)/3)*4);
        for(std::size_t i=0;i<data.size();i+=3) {
            const std::uint32_t packed=(std::uint32_t(data[i])<<16)|
                (i+1<data.size()?std::uint32_t(data[i+1])<<8:0)|(i+2<data.size()?data[i+2]:0);
            encoded+=alphabet[(packed>>18)&63];encoded+=alphabet[(packed>>12)&63];
            encoded+=i+1<data.size()?alphabet[(packed>>6)&63]:'=';
            encoded+=i+2<data.size()?alphabet[packed&63]:'=';
        }
        field.set_val(tree.copy_to_arena(ryml::to_csubstr(encoded)));changed=true;
    }
    if(!changed)return true;
    auto json=ryml::emitrs_json<std::string>(tree);
    if(!binary){bytes.assign(json.begin(),json.end());return true;}
    while(json.size()%4)json+=' ';
    if(json.size()>std::numeric_limits<std::uint32_t>::max()-20-(bytes.size()-tail))return false;
    std::vector<std::uint8_t> packed(bytes.begin(),bytes.begin()+20);
    packed.insert(packed.end(),json.begin(),json.end());packed.insert(packed.end(),bytes.begin()+tail,bytes.end());
    auto write=[&](std::size_t offset,std::uint32_t value){std::memcpy(packed.data()+offset,&value,4);};
    write(8,static_cast<std::uint32_t>(packed.size()));write(12,static_cast<std::uint32_t>(json.size()));
    bytes=std::move(packed);return true;
}

utils::Entity SourceNode(filament::gltfio::FilamentAsset& asset,std::uint32_t index) {
    utils::Entity entity{};
    const auto name="__bazzalt_node_"+std::to_string(index);
    return asset.getEntitiesByName(name.c_str(),&entity,1)==1?entity:utils::Entity{};
}

filament::math::mat4f ToFilamentMatrix(const Mat4& value) {
    for (const float element : value.Values)
        if (!std::isfinite(element)) return filament::math::mat4f{};
    return {filament::math::float4{value(0,0), value(1,0), value(2,0), value(3,0)},
            filament::math::float4{value(0,1), value(1,1), value(2,1), value(3,1)},
            filament::math::float4{value(0,2), value(1,2), value(2,2), value(3,2)},
            filament::math::float4{value(0,3), value(1,3), value(2,3), value(3,3)}};
}

} // namespace

struct RenderAssets::Impl {
    enum class Kind { Gltf, Filamesh, Primitive };
    struct Instance {
        struct Original { utils::Entity Entity;std::size_t Primitive;filament::MaterialInstance* Material; };
        Kind Type = Kind::Gltf;
        UUID Asset{};
        UUID EditorOwner{};
        std::vector<UUID> Materials;
        filament::gltfio::FilamentAsset* Gltf = nullptr;
        std::shared_ptr<filament::gltfio::FilamentAsset> GltfOwner;
        utils::Entity SelectedGltfEntity{};
        filamesh::MeshReader::Mesh Filamesh{};
        utils::Entity PrimitiveEntity{};
        filament::VertexBuffer* PrimitiveVertices=nullptr;
        filament::IndexBuffer* PrimitiveIndices=nullptr;
        filament::MaterialInstance* PrimitiveMaterial=nullptr;
        std::vector<filament::MaterialInstance*> MaterialInstances;
        bool AddedToScene = false;
        bool DebugWireframeAdded = false;
        std::vector<Original> Originals;
        struct Override { utils::Entity Entity;std::size_t Primitive;UUID Asset;filament::MaterialInstance* Base;filament::MaterialInstance* Value=nullptr; };
        std::vector<Override> Overrides;
        std::shared_ptr<ModelGeometryAsset> Geometry;
        std::uint32_t SourceIndex=Mesh::EntireAsset;
    };

    filament::Engine& Engine;
    filament::Scene& Scene;
    filament::gltfio::MaterialProvider* GltfMaterials = nullptr;
    filament::gltfio::TextureProvider* GltfTextures = nullptr;
    filament::gltfio::TextureProvider* StandaloneTextures = nullptr;
    filament::gltfio::AssetLoader* GltfLoader = nullptr;
    struct CachedModel {
        UUID Asset;
        std::filesystem::path CachePath;
        std::weak_ptr<filament::gltfio::FilamentAsset> Resource;
    };
    std::unordered_map<UUID, CachedModel> Models;
    struct CachedGeometry { std::filesystem::path Path;std::weak_ptr<ModelGeometryAsset> Geometry; };
    std::unordered_map<UUID,CachedGeometry> GeometryCache;
    Handle NextHandle = 1;
    std::unordered_map<Handle, Instance> Instances;
    std::vector<utils::Entity> EditorSuspended;
    std::unordered_map<UUID, filament::Material*> Materials;
    std::unordered_map<UUID, std::vector<std::uint8_t>> MaterialPackages;
    std::unordered_map<UUID, std::filesystem::file_time_type> MaterialModified;
    std::vector<filament::Material*> RetiredMaterials;
    std::unordered_map<UUID, filament::Texture*> Textures;
    filament::Material* DebugUnlit=nullptr;filament::Material* DebugLighting=nullptr;filament::Material* DebugOverdraw=nullptr;filament::Material* DebugWireframe=nullptr;
    filament::Material* PrimitiveMaterial=nullptr;
    filament::MaterialInstance* DebugUnlitInstance=nullptr;filament::MaterialInstance* DebugLightingInstance=nullptr;filament::MaterialInstance* DebugOverdrawInstance=nullptr;filament::MaterialInstance* DebugWireframeInstance=nullptr;
    std::string DebugMode="lit";

    Impl(filament::Engine& engine, filament::Scene& scene) : Engine(engine), Scene(scene) {
        GltfMaterials = filament::gltfio::createJitShaderProvider(&Engine);
        GltfTextures = filament::gltfio::createStbProvider(&Engine);
        StandaloneTextures = filament::gltfio::createStbProvider(&Engine);
        if (GltfMaterials) GltfLoader = filament::gltfio::AssetLoader::create({&Engine, GltfMaterials});
        DebugUnlit=filament::Material::Builder().package(Embedded::EditorUnlitFilamat,Embedded::EditorUnlitFilamatSize).build(Engine);DebugLighting=filament::Material::Builder().package(Embedded::EditorLightingOnlyFilamat,Embedded::EditorLightingOnlyFilamatSize).build(Engine);DebugOverdraw=filament::Material::Builder().package(Embedded::EditorOverdrawFilamat,Embedded::EditorOverdrawFilamatSize).build(Engine);DebugWireframe=filament::Material::Builder().package(Embedded::EditorWireframeFilamat,Embedded::EditorWireframeFilamatSize).build(Engine);
        if(DebugUnlit)DebugUnlitInstance=DebugUnlit->createInstance();if(DebugLighting)DebugLightingInstance=DebugLighting->createInstance();if(DebugOverdraw)DebugOverdrawInstance=DebugOverdraw->createInstance();if(DebugWireframe)DebugWireframeInstance=DebugWireframe->createInstance();
        PrimitiveMaterial=filament::Material::Builder().package(Embedded::PrimitiveLitFilamat,Embedded::PrimitiveLitFilamatSize).build(Engine);
    }

    filament::Material* LoadMaterial(UUID id) {
        if (!id) return nullptr;
        std::vector<std::uint8_t> ownedBytes;
        const auto embedded = GetEmbeddedPostProcessShader(id);
        const std::uint8_t* data = embedded.Data;
        std::size_t size = embedded.Size;
        if (data == nullptr) {
            const auto asset = AssetManager::GetAsset(id);
            if (!asset || asset->State != AssetState::Ready) return nullptr;
            auto path=asset->CachePath;
            const auto extension=LowerExtension(asset->SourcePath);
            if(extension==".mat"||extension==".shad")path+=".filamat";
            else if(extension!=".filamat")return nullptr;
            std::error_code error;const auto modified=std::filesystem::last_write_time(path,error);
            if(error)return nullptr;
            if(auto found=Materials.find(id);found!=Materials.end()&&MaterialModified[id]==modified)return found->second;
            MaterialModified[id]=modified;
            ownedBytes = ReadBytes(path);
            data = ownedBytes.data(); size = ownedBytes.size();
        }
        if (data == nullptr || size == 0) return nullptr;
        if(auto found=Materials.find(id);found!=Materials.end()) {
            if(embedded.Data || MaterialPackages[id]==ownedBytes)return found->second;
        }
        auto* material = filament::Material::Builder().package(data, size).build(Engine);
        if (material) {if(auto found=Materials.find(id);found!=Materials.end())RetiredMaterials.push_back(found->second);Materials[id]=material;MaterialPackages[id]=std::move(ownedBytes);}
        return material;
    }

    filament::Texture* LoadTexture(UUID id, bool srgb) {
        if (!id || !StandaloneTextures) return nullptr;
        if (const auto found = Textures.find(id); found != Textures.end()) return found->second;
        const auto asset = AssetManager::GetAsset(id);
        if (!asset || asset->State != AssetState::Ready) return nullptr;
        const std::string extension = LowerExtension(asset->SourcePath);
        if(extension==".hdr"||extension==".exr"){
            auto folder=asset->CachePath;folder+=".environment";
            const auto bytes=ReadBytes(folder/"texture"/asset->CachePath.stem()/"skybox.hdr");
            if(bytes.empty()||bytes.size()>256u*1024u*1024u)return nullptr;
            int width=0,height=0,channels=0;
            float* pixels=stbi_loadf_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels,4);
            if(!pixels)return nullptr;
            if(width<1||height<1||width>16384||height>16384){stbi_image_free(pixels);return nullptr;}
            auto* texture=filament::Texture::Builder().width(width).height(height).levels(1).sampler(filament::Texture::Sampler::SAMPLER_2D).format(filament::Texture::InternalFormat::RGBA16F).build(Engine);
            if(!texture){stbi_image_free(pixels);return nullptr;}
            texture->setImage(Engine,0,filament::Texture::PixelBufferDescriptor(pixels,std::size_t(width)*height*4*sizeof(float),filament::Texture::Format::RGBA,filament::Texture::Type::FLOAT,[](void* buffer,std::size_t,void*){stbi_image_free(buffer);}));
            Textures.emplace(id,texture);return texture;
        }
        const char* mime = extension == ".png" ? "image/png" :
            (extension == ".jpg" || extension == ".jpeg" ? "image/jpeg" : nullptr);
        const auto bytes = ReadBytes(asset->CachePath);
        if (!mime || bytes.empty()) return nullptr;
        auto* texture = StandaloneTextures->pushTexture(bytes.data(), bytes.size(), mime,
            srgb ? filament::gltfio::TextureProvider::TextureFlags::sRGB
                 : filament::gltfio::TextureProvider::TextureFlags::NONE);
        if (!texture) return nullptr;
        StandaloneTextures->waitForCompletion(); StandaloneTextures->updateQueue();
        while (auto* completed = StandaloneTextures->popTexture()) {
            if (completed == texture && StandaloneTextures->getPopMessage() == nullptr)
                Textures.emplace(id, texture);
            else if (completed == texture) Engine.destroy(texture);
        }
        return Textures.contains(id) ? texture : nullptr;
    }

    template<class T> void ApplyRenderable(utils::Entity entity, const T& component) {
        auto& renderables = Engine.getRenderableManager();
        const auto instance = renderables.getInstance(entity);
        if (!instance) return;
        renderables.setLayerMask(instance, 0xff, component.LayerMask);
        renderables.setCastShadows(instance, component.CastShadows);
        renderables.setReceiveShadows(instance, component.ReceiveShadows);
    }
    void ApplyMaterialValues(filament::MaterialInstance* instance,UUID asset) {
        auto* services=GetMaterialServices();auto material=Bazzalt::Material::Load(asset);
        std::vector<filament::Material::ParameterInfo> actual(instance->getMaterial()->getParameterCount());instance->getMaterial()->getParameters(actual.data(),actual.size());
        for(const auto& p:material.GetParameters()) {
            const auto found=std::find_if(actual.begin(),actual.end(),[&](const auto& a){return p.Name==a.name;});
            if(found==actual.end()||found->count>1||found->isSubpass)continue;
            Detail::MaterialValue v;if(!services->Get(asset,p.Name.c_str(),&v))continue;
            const auto* name=p.Name.c_str();using U=filament::Material::ParameterType;
            if(p.Type==ShaderParameterType::Texture2D){if(found->isSampler&&found->samplerType==filament::Material::SamplerType::SAMPLER_2D)instance->setParameter(name,LoadTexture(v.Texture,false),filament::TextureSampler(filament::TextureSampler::MagFilter::LINEAR,filament::TextureSampler::WrapMode::REPEAT));continue;}
            if(found->isSampler)continue;
            switch(p.Type){
                case ShaderParameterType::Float:if(found->type==U::FLOAT)instance->setParameter(name,v.Numbers[0]);break;
                case ShaderParameterType::Float2:if(found->type==U::FLOAT2)instance->setParameter(name,filament::math::float2{v.Numbers[0],v.Numbers[1]});break;
                case ShaderParameterType::Float3:if(found->type==U::FLOAT3)instance->setParameter(name,filament::math::float3{v.Numbers[0],v.Numbers[1],v.Numbers[2]});break;
                case ShaderParameterType::Float4:if(found->type==U::FLOAT4)instance->setParameter(name,filament::math::float4{v.Numbers[0],v.Numbers[1],v.Numbers[2],v.Numbers[3]});break;
                case ShaderParameterType::Integer:if(found->type==U::INT)instance->setParameter(name,v.Integer);break;
                case ShaderParameterType::Boolean:if(found->type==U::BOOL)instance->setParameter(name,v.Boolean);break;
                case ShaderParameterType::Matrix3:if(found->type==U::MAT3)instance->setParameter(name,filament::math::mat3f{filament::math::float3{v.Numbers[0],v.Numbers[1],v.Numbers[2]},filament::math::float3{v.Numbers[3],v.Numbers[4],v.Numbers[5]},filament::math::float3{v.Numbers[6],v.Numbers[7],v.Numbers[8]}});break;
                case ShaderParameterType::Matrix4:if(found->type==U::MAT4)instance->setParameter(name,filament::math::mat4f{filament::math::float4{v.Numbers[0],v.Numbers[1],v.Numbers[2],v.Numbers[3]},filament::math::float4{v.Numbers[4],v.Numbers[5],v.Numbers[6],v.Numbers[7]},filament::math::float4{v.Numbers[8],v.Numbers[9],v.Numbers[10],v.Numbers[11]},filament::math::float4{v.Numbers[12],v.Numbers[13],v.Numbers[14],v.Numbers[15]}});break;
                default:break;
            }
        }
    }
    void UpdateMaterials(Instance& value) {
        for(auto& override:value.Overrides){
            const auto material=Bazzalt::Material::Load(override.Asset);
            auto* shader=material.IsValid()?LoadMaterial(material.GetShader().GetAssetUUID()):LoadMaterial(override.Asset);
            if(shader&&shader->getMaterialDomain()!=filament::MaterialDomain::SURFACE)shader=nullptr;
            if(shader){auto& manager=Engine.getRenderableManager();auto enabled=manager.getEnabledAttributesAt(manager.getInstance(override.Entity),override.Primitive);auto required=shader->getRequiredAttributes();if((enabled & required)!=required)shader=nullptr;}
            if(shader && (!override.Value || override.Value->getMaterial()!=shader)){
                Restore(value);auto* old=override.Value;override.Value=shader->createInstance();
                const auto ri=Engine.getRenderableManager().getInstance(override.Entity);
                Engine.getRenderableManager().setMaterialInstanceAt(ri,override.Primitive,override.Value);
                if(old)Engine.destroy(old);ApplyDebug(value);
            }else if(!shader&&override.Value){Restore(value);const auto ri=Engine.getRenderableManager().getInstance(override.Entity);Engine.getRenderableManager().setMaterialInstanceAt(ri,override.Primitive,override.Base);Engine.destroy(override.Value);override.Value=nullptr;ApplyDebug(value);}
            if(override.Value&&material.IsValid())ApplyMaterialValues(override.Value,override.Asset);
        }
    }
    void RegisterOverrides(Instance& value,const Mesh& component) {
        const auto apply=[&](utils::Entity entity){auto& manager=Engine.getRenderableManager();const auto ri=manager.getInstance(entity);if(!ri)return;
            for(std::size_t i=0;i<manager.getPrimitiveCount(ri);++i){const auto id=component.MaterialAsset?component.MaterialAsset:i<component.Materials.size()?component.Materials[i]:UUID{};if(id)value.Overrides.push_back({entity,i,id,manager.getMaterialInstanceAt(ri,i)});}};
        if(value.Type==Kind::Gltf){if(value.SelectedGltfEntity)apply(value.SelectedGltfEntity);else for(std::size_t i=0;i<value.Gltf->getEntityCount();++i)apply(value.Gltf->getEntities()[i]);}else apply(value.Type==Kind::Primitive?value.PrimitiveEntity:value.Filamesh.renderable);
        UpdateMaterials(value);
    }
    void Restore(Instance& value){if(value.DebugWireframeAdded){Scene.remove(value.Gltf->getWireframe());if(value.AddedToScene)Scene.addEntities(value.Gltf->getEntities(),value.Gltf->getEntityCount());value.DebugWireframeAdded=false;}auto& manager=Engine.getRenderableManager();for(const auto& original:value.Originals){auto instance=manager.getInstance(original.Entity);if(instance&&original.Primitive<manager.getPrimitiveCount(instance))manager.setMaterialInstanceAt(instance,original.Primitive,original.Material);}value.Originals.clear();}
    void ApplyDebug(Instance& value){Restore(value);if(DebugMode=="wireframe"&&value.Type==Kind::Gltf&&!value.SelectedGltfEntity&&value.AddedToScene){Scene.removeEntities(value.Gltf->getEntities(),value.Gltf->getEntityCount());Scene.addEntity(value.Gltf->getWireframe());value.DebugWireframeAdded=true;return;}filament::MaterialInstance* material=DebugMode=="unlit"?DebugUnlitInstance:DebugMode=="lighting_only"?DebugLightingInstance:DebugMode=="overdraw"?DebugOverdrawInstance:DebugMode=="wireframe"?DebugWireframeInstance:nullptr;if(!material)return;auto& manager=Engine.getRenderableManager();const auto apply=[&](utils::Entity entity){auto instance=manager.getInstance(entity);if(!instance)return;for(std::size_t primitive=0;primitive<manager.getPrimitiveCount(instance);++primitive){value.Originals.push_back({entity,primitive,manager.getMaterialInstanceAt(instance,primitive)});manager.setMaterialInstanceAt(instance,primitive,material);}};if(value.Type==Kind::Gltf){if(value.SelectedGltfEntity)apply(value.SelectedGltfEntity);else for(std::size_t i=0;i<value.Gltf->getEntityCount();++i)apply(value.Gltf->getEntities()[i]);}else apply(value.Type==Kind::Primitive?value.PrimitiveEntity:value.Filamesh.renderable);}
};

RenderAssets::RenderAssets(filament::Engine& engine, filament::Scene& scene)
    : m_impl(std::make_unique<Impl>(engine, scene)) {}
RenderAssets::~RenderAssets() { Shutdown(); }

bool RenderAssets::PreparePostProcessEffect(const CustomPostProcessEffect& effect) {
    if (!m_impl || !effect.ShaderAsset) return false;
    auto* material = m_impl->LoadMaterial(effect.ShaderAsset);
    if (!material || material->getMaterialDomain() != filament::MaterialDomain::POST_PROCESS)
        return false;
    for (const auto& parameter : effect.Parameters) {
        if (parameter.Name.empty() || !material->hasParameter(parameter.Name)) return false;
        if (parameter.Type == PostProcessParameterType::Texture &&
            (!parameter.TextureAsset || !m_impl->LoadTexture(parameter.TextureAsset, false)))
            return false;
    }
    return true;
}

RenderAssets::Handle RenderAssets::CreateMesh(const Mesh& component) {
    return CreateMesh(component, {});
}

RenderAssets::Handle RenderAssets::CreateMesh(const Mesh& component, UUID modelInstance) {
    if (!m_impl || !component.MeshAsset) return InvalidHandle;
    const auto asset = AssetManager::GetAsset(component.MeshAsset);
    if (!asset || asset->State != AssetState::Ready) return InvalidHandle;

    Impl::Instance instance;
    instance.Asset = component.MeshAsset; instance.Materials = component.Materials;
    const std::string extension = LowerExtension(asset->SourcePath);
    if (extension == ".gltf" || extension == ".glb") {
        if (!m_impl->GltfLoader || !m_impl->GltfTextures) return InvalidHandle;
        instance.Type = Impl::Kind::Gltf;
        instance.SourceIndex=component.ModelNodeIndex;
        if(auto found=m_impl->GeometryCache.find(component.MeshAsset);found!=m_impl->GeometryCache.end()&&found->second.Path==asset->CachePath)
            instance.Geometry=found->second.Geometry.lock();
        bool shared = modelInstance && component.ModelNodeIndex != Mesh::EntireAsset;
        if (shared) {
            auto found = m_impl->Models.find(modelInstance);
            if (found != m_impl->Models.end() && found->second.Asset == component.MeshAsset &&
                found->second.CachePath == asset->CachePath)
                instance.GltfOwner = found->second.Resource.lock();
        }
        if (instance.GltfOwner) {
            const auto entity=SourceNode(*instance.GltfOwner,component.ModelNodeIndex);
            for (const auto& [_, existing] : m_impl->Instances) {
                if (existing.GltfOwner==instance.GltfOwner && existing.SelectedGltfEntity==entity) {
                    // A duplicated child is an independent object, not a second
                    // controller for the same native transform/material slots.
                    instance.GltfOwner.reset();shared=false;break;
                }
            }
        }
        if (!instance.GltfOwner) {
            // Convert before allocation, and own every created asset immediately:
            // exceptions, invalid node indices and failed resource loads cannot
            // leave material instances alive until engine destruction.
            const std::string cachedSource = PathUtf8(asset->CachePath);
            auto bytes = ReadBytes(asset->CachePath);
            if (bytes.empty() || !EmbedGltfBuffers(bytes,asset->CachePath)) return InvalidHandle;
            if(!instance.Geometry){instance.Geometry=ReadModelGeometry(bytes);m_impl->GeometryCache.insert_or_assign(component.MeshAsset,Impl::CachedGeometry{asset->CachePath,instance.Geometry});}
            if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max()) return InvalidHandle;
            auto* loaded = m_impl->GltfLoader->createAsset(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
            if (!loaded) return InvalidHandle;
            instance.GltfOwner = {loaded, [impl=m_impl.get()](auto* value) {
                impl->Scene.removeEntities(value->getEntities(), value->getEntityCount());
                impl->GltfLoader->destroyAsset(value);
            }};
            filament::gltfio::ResourceLoader resources({&m_impl->Engine, cachedSource.c_str(), true});
            resources.addTextureProvider("image/png", m_impl->GltfTextures);
            resources.addTextureProvider("image/jpeg", m_impl->GltfTextures);
            // Read external files through std::filesystem's native Unicode path
            // support instead of relying on a third-party narrow fopen.
            for (std::size_t index=0; index<loaded->getResourceUriCount(); ++index) {
                const auto* uri=loaded->getResourceUris()[index];
                if (!uri || std::string_view(uri).starts_with("data:")) continue;
                auto data=std::make_unique<std::vector<std::uint8_t>>(ReadGltfResource(asset->CachePath,uri));
                if (data->empty()) return InvalidHandle;
                auto* storage=data.release();
                resources.addResourceData(uri, {storage->data(), storage->size(), [](void*,std::size_t,void* user) {
                    delete static_cast<std::vector<std::uint8_t>*>(user);
                }, storage});
            }
            if (!resources.loadResources(loaded)) return InvalidHandle;
            loaded->releaseSourceData();
            if (shared) m_impl->Models.insert_or_assign(modelInstance, Impl::CachedModel{component.MeshAsset,asset->CachePath,instance.GltfOwner});
        }
        instance.Gltf = instance.GltfOwner.get();
        if (component.ModelNodeIndex != Mesh::EntireAsset) {
            instance.SelectedGltfEntity = SourceNode(*instance.Gltf,component.ModelNodeIndex);
            if (!instance.SelectedGltfEntity || !m_impl->Engine.getRenderableManager().hasComponent(instance.SelectedGltfEntity)) return InvalidHandle;
            auto& transforms = m_impl->Engine.getTransformManager();
            const auto selectedTransform = transforms.getInstance(instance.SelectedGltfEntity);
            if (selectedTransform) transforms.setParent(selectedTransform, {});
            m_impl->Scene.addEntity(instance.SelectedGltfEntity);
        } else {
            m_impl->Scene.addEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
        }
        instance.AddedToScene = true;
        if (component.ModelNodeIndex != Mesh::EntireAsset)
            m_impl->ApplyRenderable(instance.SelectedGltfEntity, component);
        else for (std::size_t index = 0; index < instance.Gltf->getEntityCount(); ++index)
            m_impl->ApplyRenderable(instance.Gltf->getEntities()[index], component);
    } else if (extension == ".filamesh" || extension == ".obj" || extension == ".fbx") {
        instance.Type = Impl::Kind::Filamesh;
        filamesh::MeshReader::MaterialRegistry registry;
        if(!m_impl->PrimitiveMaterial)return InvalidHandle;
        auto* fallback=m_impl->PrimitiveMaterial->createInstance();fallback->setParameter("baseColor",filament::math::float4{1,1,1,1});instance.MaterialInstances.push_back(fallback);
        registry.registerMaterialInstance(utils::CString("DefaultMaterial"),fallback);
        instance.Filamesh = filamesh::MeshReader::loadMeshFromFile(&m_impl->Engine,
            utils::Path(PathUtf8(asset->CachePath)), registry);
        if (!instance.Filamesh.renderable) {
            for (auto* material : instance.MaterialInstances) m_impl->Engine.destroy(material);
            return InvalidHandle;
        }
        m_impl->Scene.addEntity(instance.Filamesh.renderable); instance.AddedToScene = true;
        m_impl->ApplyRenderable(instance.Filamesh.renderable, component);
    } else return InvalidHandle;

    m_impl->RegisterOverrides(instance,component);
    const Handle handle = m_impl->NextHandle++;
    auto [stored,_]=m_impl->Instances.emplace(handle, std::move(instance));m_impl->ApplyDebug(stored->second);
    return handle;
}

void RenderAssets::UpdateMesh(Handle handle, const Mat4& transform, const Mesh& component) {
    if (!m_impl) return;
    const auto found = m_impl->Instances.find(handle); if (found == m_impl->Instances.end()) return;
    auto& instance = found->second;
    m_impl->UpdateMaterials(instance);
    const utils::Entity root = instance.Type == Impl::Kind::Gltf
        ? (instance.SelectedGltfEntity ? instance.SelectedGltfEntity : instance.Gltf->getRoot())
        : instance.Filamesh.renderable;
    auto& transforms = m_impl->Engine.getTransformManager();
    const auto transformInstance = transforms.getInstance(root);
    if (transformInstance) transforms.setTransform(transformInstance, ToFilamentMatrix(transform));
    const bool visibilityChanged=component.Visible != instance.AddedToScene;
    if (visibilityChanged) {
        if (instance.Type == Impl::Kind::Gltf) {
            if (instance.SelectedGltfEntity) {
                if (component.Visible) m_impl->Scene.addEntity(instance.SelectedGltfEntity);
                else m_impl->Scene.remove(instance.SelectedGltfEntity);
            } else {
                if (component.Visible) m_impl->Scene.addEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
                else m_impl->Scene.removeEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
            }
        } else {
            if (component.Visible) m_impl->Scene.addEntity(root); else m_impl->Scene.remove(root);
        }
        instance.AddedToScene = component.Visible;
    }
    if (instance.Type == Impl::Kind::Gltf) {
        if (instance.SelectedGltfEntity) m_impl->ApplyRenderable(instance.SelectedGltfEntity, component);
        else for (std::size_t index = 0; index < instance.Gltf->getEntityCount(); ++index)
            m_impl->ApplyRenderable(instance.Gltf->getEntities()[index], component);
    } else m_impl->ApplyRenderable(root, component);
    if(visibilityChanged&&m_impl->DebugMode!="lit")m_impl->ApplyDebug(instance);
}

void RenderAssets::DestroyMesh(Handle handle) {
    if (!m_impl) return;
    const auto found = m_impl->Instances.find(handle); if (found == m_impl->Instances.end()) return;
    auto& instance = found->second;
    m_impl->Restore(instance);
    for(auto& override:instance.Overrides)if(override.Value){const auto ri=m_impl->Engine.getRenderableManager().getInstance(override.Entity);if(ri)m_impl->Engine.getRenderableManager().setMaterialInstanceAt(ri,override.Primitive,override.Base);m_impl->Engine.destroy(override.Value);}
    if (instance.Type == Impl::Kind::Gltf) {
        if (instance.AddedToScene) {
            if (instance.SelectedGltfEntity) m_impl->Scene.remove(instance.SelectedGltfEntity);
            else m_impl->Scene.removeEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
        }
        instance.GltfOwner.reset();
    } else if(instance.Type == Impl::Kind::Filamesh) {
        if (instance.AddedToScene) m_impl->Scene.remove(instance.Filamesh.renderable);
        m_impl->Engine.destroy(instance.Filamesh.renderable);
        m_impl->Engine.destroy(instance.Filamesh.vertexBuffer);
        m_impl->Engine.destroy(instance.Filamesh.indexBuffer);
        m_impl->Engine.getEntityManager().destroy(instance.Filamesh.renderable);
        for (auto* material : instance.MaterialInstances) m_impl->Engine.destroy(material);
    } else {
        if(instance.AddedToScene)m_impl->Scene.remove(instance.PrimitiveEntity);
        m_impl->Engine.destroy(instance.PrimitiveEntity);m_impl->Engine.destroy(instance.PrimitiveVertices);m_impl->Engine.destroy(instance.PrimitiveIndices);m_impl->Engine.destroy(instance.PrimitiveMaterial);m_impl->Engine.getEntityManager().destroy(instance.PrimitiveEntity);
    }
    m_impl->Instances.erase(found);
}

RenderAssets::Handle RenderAssets::CreatePrimitive(const PrimitiveObject& component){
    if(!m_impl||!m_impl->PrimitiveMaterial)return InvalidHandle;auto geometry=BuildPrimitiveGeometry(component);if(geometry.Vertices.empty()||geometry.Indices.empty())return InvalidHandle;
    Impl::Instance instance;instance.Type=Impl::Kind::Primitive;instance.PrimitiveMaterial=m_impl->PrimitiveMaterial->createInstance();instance.PrimitiveMaterial->setParameter("baseColor",filament::math::float4{component.Color.X,component.Color.Y,component.Color.Z,component.Color.W});
    instance.PrimitiveVertices=filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(geometry.Vertices.size())).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,sizeof(PrimitiveVertex)).attribute(filament::VertexAttribute::TANGENTS,0,filament::VertexBuffer::AttributeType::FLOAT4,12,sizeof(PrimitiveVertex)).attribute(filament::VertexAttribute::UV0,0,filament::VertexBuffer::AttributeType::FLOAT2,28,sizeof(PrimitiveVertex)).build(m_impl->Engine);
    auto* vertices=new std::vector<PrimitiveVertex>(std::move(geometry.Vertices));instance.PrimitiveVertices->setBufferAt(m_impl->Engine,0,{vertices->data(),vertices->size()*sizeof(PrimitiveVertex),[](void*,size_t,void* user){delete static_cast<std::vector<PrimitiveVertex>*>(user);},vertices});
    instance.PrimitiveIndices=filament::IndexBuffer::Builder().indexCount(static_cast<std::uint32_t>(geometry.Indices.size())).bufferType(filament::IndexBuffer::IndexType::UINT).build(m_impl->Engine);auto* indices=new std::vector<std::uint32_t>(std::move(geometry.Indices));instance.PrimitiveIndices->setBuffer(m_impl->Engine,{indices->data(),indices->size()*sizeof(std::uint32_t),[](void*,size_t,void* user){delete static_cast<std::vector<std::uint32_t>*>(user);},indices});
    instance.PrimitiveEntity=m_impl->Engine.getEntityManager().create();m_impl->Engine.getTransformManager().create(instance.PrimitiveEntity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{geometry.Extents.X,geometry.Extents.Y,geometry.Extents.Z}}).material(0,instance.PrimitiveMaterial).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,instance.PrimitiveVertices,instance.PrimitiveIndices).castShadows(component.CastShadows).receiveShadows(component.ReceiveShadows).build(m_impl->Engine,instance.PrimitiveEntity);m_impl->Scene.addEntity(instance.PrimitiveEntity);instance.AddedToScene=true;m_impl->ApplyRenderable(instance.PrimitiveEntity,component);
    auto handle=m_impl->NextHandle++;auto [stored,_]=m_impl->Instances.emplace(handle,std::move(instance));Mesh material;material.MaterialAsset=component.MaterialAsset;m_impl->RegisterOverrides(stored->second,material);m_impl->UpdateMaterials(stored->second);m_impl->ApplyDebug(stored->second);return handle;
}

void RenderAssets::UpdatePrimitive(Handle handle,const Mat4& transform,const PrimitiveObject& component){if(!m_impl)return;auto found=m_impl->Instances.find(handle);if(found==m_impl->Instances.end()||found->second.Type!=Impl::Kind::Primitive)return;auto& value=found->second;auto ti=m_impl->Engine.getTransformManager().getInstance(value.PrimitiveEntity);if(ti)m_impl->Engine.getTransformManager().setTransform(ti,ToFilamentMatrix(transform));value.PrimitiveMaterial->setParameter("baseColor",filament::math::float4{component.Color.X,component.Color.Y,component.Color.Z,component.Color.W});if(component.Visible!=value.AddedToScene){if(component.Visible)m_impl->Scene.addEntity(value.PrimitiveEntity);else m_impl->Scene.remove(value.PrimitiveEntity);value.AddedToScene=component.Visible;}m_impl->ApplyRenderable(value.PrimitiveEntity,component);m_impl->UpdateMaterials(value);}

void RenderAssets::Update() {
    if (m_impl && m_impl->GltfTextures) m_impl->GltfTextures->updateQueue();
    if (m_impl && m_impl->StandaloneTextures) m_impl->StandaloneTextures->updateQueue();
    if (m_impl) {
        for (auto it=m_impl->Models.begin(); it!=m_impl->Models.end();) {
            if (it->second.Resource.expired()) it=m_impl->Models.erase(it); else ++it;
        }
    }
}
void RenderAssets::SetEditorOwner(Handle handle,UUID owner){if(m_impl)if(auto it=m_impl->Instances.find(handle);it!=m_impl->Instances.end())it->second.EditorOwner=owner;}
std::size_t RenderAssets::GetMaterialSlotCount(Handle handle) const {
    if(!m_impl)return 0;auto found=m_impl->Instances.find(handle);if(found==m_impl->Instances.end())return 0;
    const auto& value=found->second;auto& manager=m_impl->Engine.getRenderableManager();std::size_t count=0;
    const auto inspect=[&](utils::Entity entity){auto renderable=manager.getInstance(entity);if(renderable)count=std::max(count,manager.getPrimitiveCount(renderable));};
    if(value.Type==Impl::Kind::Gltf){if(value.SelectedGltfEntity)inspect(value.SelectedGltfEntity);else for(std::size_t i=0;i<value.Gltf->getEntityCount();++i)inspect(value.Gltf->getEntities()[i]);}
    else inspect(value.Type==Impl::Kind::Primitive?value.PrimitiveEntity:value.Filamesh.renderable);
    return count;
}
std::vector<RenderAssets::EditorMeshGeometry> RenderAssets::GetEditorMeshes() const {
    std::vector<EditorMeshGeometry> result;if(!m_impl)return result;
    auto& transforms=m_impl->Engine.getTransformManager();
    for(const auto& [_,value]:m_impl->Instances){
        if(value.Type!=Impl::Kind::Gltf||!value.AddedToScene||!value.Geometry)continue;
        const auto append=[&](std::uint32_t index){
            if(index>=value.Geometry->SourceNodes.size())return;
            auto geometry=value.Geometry->SourceNodes[index];if(!geometry||geometry->Triangles.empty())return;
            auto entity=SourceNode(*value.Gltf,index);auto transform=transforms.getInstance(entity);if(!transform)return;
            const auto& native=transforms.getWorldTransform(transform);Mat4 world;
            for(int column=0;column<4;++column)for(int row=0;row<4;++row)world(row,column)=native[column][row];
            result.push_back({value.EditorOwner,world,std::move(geometry)});
        };
        if(value.SourceIndex!=Mesh::EntireAsset)append(value.SourceIndex);
        else for(std::uint32_t index=0;index<value.Geometry->SourceNodes.size();++index)append(index);
    }return result;
}
void RenderAssets::BeginEditorView(const std::unordered_set<UUID>& hidden){
    if(!m_impl)return;EndEditorView();
    for(const auto& [_,value]:m_impl->Instances){
        if(!value.AddedToScene||!hidden.contains(value.EditorOwner))continue;
        const auto suspend=[&](utils::Entity entity){if(m_impl->Scene.hasEntity(entity))m_impl->EditorSuspended.push_back(entity);};
        if(value.Type==Impl::Kind::Gltf){if(value.SelectedGltfEntity)suspend(value.SelectedGltfEntity);else{for(std::size_t i=0;i<value.Gltf->getEntityCount();++i)suspend(value.Gltf->getEntities()[i]);if(value.DebugWireframeAdded)suspend(value.Gltf->getWireframe());}}
        else suspend(value.Type==Impl::Kind::Primitive?value.PrimitiveEntity:value.Filamesh.renderable);
    }
    for(auto entity:m_impl->EditorSuspended)m_impl->Scene.remove(entity);
}
void RenderAssets::EndEditorView(){if(!m_impl)return;for(auto entity:m_impl->EditorSuspended)m_impl->Scene.addEntity(entity);m_impl->EditorSuspended.clear();}

bool RenderAssets::SetDebugMode(const std::string& mode){if(!m_impl||mode!="lit"&&mode!="unlit"&&mode!="wireframe"&&mode!="lighting_only"&&mode!="overdraw")return false;m_impl->DebugMode=mode;for(auto& [_,instance]:m_impl->Instances)m_impl->ApplyDebug(instance);return true;}

void RenderAssets::Shutdown() {
    if (!m_impl) return;
    while (!m_impl->Instances.empty()) DestroyMesh(m_impl->Instances.begin()->first);
    m_impl->Models.clear();
    m_impl->GeometryCache.clear();
    for (const auto& [id, texture] : m_impl->Textures) m_impl->Engine.destroy(texture);
    for (const auto& [id, material] : m_impl->Materials) m_impl->Engine.destroy(material);
    for(auto* material:m_impl->RetiredMaterials)m_impl->Engine.destroy(material);
    for(auto* instance:{m_impl->DebugUnlitInstance,m_impl->DebugLightingInstance,m_impl->DebugOverdrawInstance,m_impl->DebugWireframeInstance})if(instance)m_impl->Engine.destroy(instance);
    for(auto* material:{m_impl->DebugUnlit,m_impl->DebugLighting,m_impl->DebugOverdraw,m_impl->DebugWireframe})if(material)m_impl->Engine.destroy(material);
    if(m_impl->PrimitiveMaterial)m_impl->Engine.destroy(m_impl->PrimitiveMaterial);
    m_impl->Textures.clear(); m_impl->Materials.clear();
    if (m_impl->GltfLoader) filament::gltfio::AssetLoader::destroy(&m_impl->GltfLoader);
    if (m_impl->GltfMaterials) { m_impl->GltfMaterials->destroyMaterials(); delete m_impl->GltfMaterials; }
    delete m_impl->GltfTextures; delete m_impl->StandaloneTextures;
    m_impl.reset();
}

} // namespace Bazzalt::Runtime
