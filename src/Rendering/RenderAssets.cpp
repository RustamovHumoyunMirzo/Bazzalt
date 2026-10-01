#include "Rendering/RenderAssets.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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
#include "Rendering/BuiltinPostProcess.h"
#include "Rendering/PrimitiveGeometry.h"
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
        std::vector<UUID> Materials;
        filament::gltfio::FilamentAsset* Gltf = nullptr;
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
    };

    filament::Engine& Engine;
    filament::Scene& Scene;
    filament::gltfio::MaterialProvider* GltfMaterials = nullptr;
    filament::gltfio::TextureProvider* GltfTextures = nullptr;
    filament::gltfio::TextureProvider* StandaloneTextures = nullptr;
    filament::gltfio::AssetLoader* GltfLoader = nullptr;
    Handle NextHandle = 1;
    std::unordered_map<Handle, Instance> Instances;
    std::unordered_map<UUID, filament::Material*> Materials;
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
        if (const auto found = Materials.find(id); found != Materials.end()) return found->second;
        std::vector<std::uint8_t> ownedBytes;
        const auto embedded = GetEmbeddedPostProcessShader(id);
        const std::uint8_t* data = embedded.Data;
        std::size_t size = embedded.Size;
        if (data == nullptr) {
            const auto asset = AssetManager::GetAsset(id);
            if (!asset || asset->State != AssetState::Ready) return nullptr;
            ownedBytes = ReadBytes(asset->CachePath);
            data = ownedBytes.data(); size = ownedBytes.size();
        }
        if (data == nullptr || size == 0) return nullptr;
        auto* material = filament::Material::Builder().package(data, size).build(Engine);
        if (material) Materials.emplace(id, material);
        return material;
    }

    filament::Texture* LoadTexture(UUID id, bool srgb) {
        if (!id || !StandaloneTextures) return nullptr;
        if (const auto found = Textures.find(id); found != Textures.end()) return found->second;
        const auto asset = AssetManager::GetAsset(id);
        if (!asset || asset->State != AssetState::Ready) return nullptr;
        const std::string extension = LowerExtension(asset->SourcePath);
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
    if (!m_impl || !component.MeshAsset) return InvalidHandle;
    const auto asset = AssetManager::GetAsset(component.MeshAsset);
    if (!asset || asset->State != AssetState::Ready) return InvalidHandle;

    Impl::Instance instance;
    instance.Asset = component.MeshAsset; instance.Materials = component.Materials;
    const std::string extension = LowerExtension(asset->SourcePath);
    if (extension == ".gltf" || extension == ".glb") {
        if (!m_impl->GltfLoader || !m_impl->GltfTextures) return InvalidHandle;
        const auto bytes = ReadBytes(asset->CachePath);
        if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max()) return InvalidHandle;
        instance.Type = Impl::Kind::Gltf;
        instance.Gltf = m_impl->GltfLoader->createAsset(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        if (!instance.Gltf) return InvalidHandle;
        const std::string cachedSource = asset->CachePath.string();
        filament::gltfio::ResourceLoader resources({&m_impl->Engine, cachedSource.c_str(), true});
        resources.addTextureProvider("image/png", m_impl->GltfTextures);
        resources.addTextureProvider("image/jpeg", m_impl->GltfTextures);
        if (!resources.loadResources(instance.Gltf)) {
            m_impl->GltfLoader->destroyAsset(instance.Gltf); return InvalidHandle;
        }
        instance.Gltf->releaseSourceData();
        if (component.ModelNodeIndex != Mesh::EntireAsset) {
            if (component.ModelNodeIndex >= instance.Gltf->getEntityCount()) {
                m_impl->GltfLoader->destroyAsset(instance.Gltf); return InvalidHandle;
            }
            instance.SelectedGltfEntity = instance.Gltf->getEntities()[component.ModelNodeIndex];
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
        for (std::size_t index = 0; index < component.Materials.size(); ++index) {
            if (auto* material = m_impl->LoadMaterial(component.Materials[index])) {
                auto* materialInstance = material->createInstance();
                instance.MaterialInstances.push_back(materialInstance);
                const std::string name = index == 0 ? "DefaultMaterial" : "Material" + std::to_string(index);
                registry.registerMaterialInstance(utils::CString(name.c_str()), materialInstance);
            }
        }
        instance.Filamesh = filamesh::MeshReader::loadMeshFromFile(&m_impl->Engine,
            utils::Path(asset->CachePath.string()), registry);
        if (!instance.Filamesh.renderable) {
            for (auto* material : instance.MaterialInstances) m_impl->Engine.destroy(material);
            return InvalidHandle;
        }
        m_impl->Scene.addEntity(instance.Filamesh.renderable); instance.AddedToScene = true;
        m_impl->ApplyRenderable(instance.Filamesh.renderable, component);
    } else return InvalidHandle;

    const Handle handle = m_impl->NextHandle++;
    auto [stored,_]=m_impl->Instances.emplace(handle, std::move(instance));m_impl->ApplyDebug(stored->second);
    return handle;
}

void RenderAssets::UpdateMesh(Handle handle, const Mat4& transform, const Mesh& component) {
    if (!m_impl) return;
    const auto found = m_impl->Instances.find(handle); if (found == m_impl->Instances.end()) return;
    auto& instance = found->second;
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
    if (instance.Type == Impl::Kind::Gltf) {
        if (instance.AddedToScene) {
            if (instance.SelectedGltfEntity) m_impl->Scene.remove(instance.SelectedGltfEntity);
            else m_impl->Scene.removeEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
        }
        m_impl->GltfLoader->destroyAsset(instance.Gltf);
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
    instance.PrimitiveVertices=filament::VertexBuffer::Builder().vertexCount(static_cast<std::uint32_t>(geometry.Vertices.size())).bufferCount(1).attribute(filament::VertexAttribute::POSITION,0,filament::VertexBuffer::AttributeType::FLOAT3,0,sizeof(PrimitiveVertex)).attribute(filament::VertexAttribute::TANGENTS,0,filament::VertexBuffer::AttributeType::FLOAT4,12,sizeof(PrimitiveVertex)).build(m_impl->Engine);
    auto* vertices=new std::vector<PrimitiveVertex>(std::move(geometry.Vertices));instance.PrimitiveVertices->setBufferAt(m_impl->Engine,0,{vertices->data(),vertices->size()*sizeof(PrimitiveVertex),[](void*,size_t,void* user){delete static_cast<std::vector<PrimitiveVertex>*>(user);},vertices});
    instance.PrimitiveIndices=filament::IndexBuffer::Builder().indexCount(static_cast<std::uint32_t>(geometry.Indices.size())).bufferType(filament::IndexBuffer::IndexType::UINT).build(m_impl->Engine);auto* indices=new std::vector<std::uint32_t>(std::move(geometry.Indices));instance.PrimitiveIndices->setBuffer(m_impl->Engine,{indices->data(),indices->size()*sizeof(std::uint32_t),[](void*,size_t,void* user){delete static_cast<std::vector<std::uint32_t>*>(user);},indices});
    instance.PrimitiveEntity=m_impl->Engine.getEntityManager().create();m_impl->Engine.getTransformManager().create(instance.PrimitiveEntity);filament::RenderableManager::Builder(1).boundingBox({{0,0,0},{geometry.Extents.X,geometry.Extents.Y,geometry.Extents.Z}}).material(0,instance.PrimitiveMaterial).geometry(0,filament::RenderableManager::PrimitiveType::TRIANGLES,instance.PrimitiveVertices,instance.PrimitiveIndices).castShadows(component.CastShadows).receiveShadows(component.ReceiveShadows).build(m_impl->Engine,instance.PrimitiveEntity);m_impl->Scene.addEntity(instance.PrimitiveEntity);instance.AddedToScene=true;m_impl->ApplyRenderable(instance.PrimitiveEntity,component);
    auto handle=m_impl->NextHandle++;auto [stored,_]=m_impl->Instances.emplace(handle,std::move(instance));m_impl->ApplyDebug(stored->second);return handle;
}

void RenderAssets::UpdatePrimitive(Handle handle,const Mat4& transform,const PrimitiveObject& component){if(!m_impl)return;auto found=m_impl->Instances.find(handle);if(found==m_impl->Instances.end()||found->second.Type!=Impl::Kind::Primitive)return;auto& value=found->second;auto ti=m_impl->Engine.getTransformManager().getInstance(value.PrimitiveEntity);if(ti)m_impl->Engine.getTransformManager().setTransform(ti,ToFilamentMatrix(transform));value.PrimitiveMaterial->setParameter("baseColor",filament::math::float4{component.Color.X,component.Color.Y,component.Color.Z,component.Color.W});if(component.Visible!=value.AddedToScene){if(component.Visible)m_impl->Scene.addEntity(value.PrimitiveEntity);else m_impl->Scene.remove(value.PrimitiveEntity);value.AddedToScene=component.Visible;}m_impl->ApplyRenderable(value.PrimitiveEntity,component);}

void RenderAssets::Update() {
    if (m_impl && m_impl->GltfTextures) m_impl->GltfTextures->updateQueue();
    if (m_impl && m_impl->StandaloneTextures) m_impl->StandaloneTextures->updateQueue();
}

bool RenderAssets::SetDebugMode(const std::string& mode){if(!m_impl||mode!="lit"&&mode!="unlit"&&mode!="wireframe"&&mode!="lighting_only"&&mode!="overdraw")return false;m_impl->DebugMode=mode;for(auto& [_,instance]:m_impl->Instances)m_impl->ApplyDebug(instance);return true;}

void RenderAssets::Shutdown() {
    if (!m_impl) return;
    while (!m_impl->Instances.empty()) DestroyMesh(m_impl->Instances.begin()->first);
    for (const auto& [id, texture] : m_impl->Textures) m_impl->Engine.destroy(texture);
    for (const auto& [id, material] : m_impl->Materials) m_impl->Engine.destroy(material);
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
