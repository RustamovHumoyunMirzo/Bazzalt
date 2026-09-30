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
    enum class Kind { Gltf, Filamesh };
    struct Instance {
        Kind Type = Kind::Gltf;
        UUID Asset{};
        std::vector<UUID> Materials;
        filament::gltfio::FilamentAsset* Gltf = nullptr;
        utils::Entity SelectedGltfEntity{};
        filamesh::MeshReader::Mesh Filamesh{};
        std::vector<filament::MaterialInstance*> MaterialInstances;
        bool AddedToScene = false;
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

    Impl(filament::Engine& engine, filament::Scene& scene) : Engine(engine), Scene(scene) {
        GltfMaterials = filament::gltfio::createJitShaderProvider(&Engine);
        GltfTextures = filament::gltfio::createStbProvider(&Engine);
        StandaloneTextures = filament::gltfio::createStbProvider(&Engine);
        if (GltfMaterials) GltfLoader = filament::gltfio::AssetLoader::create({&Engine, GltfMaterials});
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

    void ApplyRenderable(utils::Entity entity, const Mesh& component) {
        auto& renderables = Engine.getRenderableManager();
        const auto instance = renderables.getInstance(entity);
        if (!instance) return;
        renderables.setLayerMask(instance, 0xff, component.LayerMask);
        renderables.setCastShadows(instance, component.CastShadows);
        renderables.setReceiveShadows(instance, component.ReceiveShadows);
    }
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
    m_impl->Instances.emplace(handle, std::move(instance));
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
    if (component.Visible != instance.AddedToScene) {
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
}

void RenderAssets::DestroyMesh(Handle handle) {
    if (!m_impl) return;
    const auto found = m_impl->Instances.find(handle); if (found == m_impl->Instances.end()) return;
    auto& instance = found->second;
    if (instance.Type == Impl::Kind::Gltf) {
        if (instance.AddedToScene) {
            if (instance.SelectedGltfEntity) m_impl->Scene.remove(instance.SelectedGltfEntity);
            else m_impl->Scene.removeEntities(instance.Gltf->getEntities(), instance.Gltf->getEntityCount());
        }
        m_impl->GltfLoader->destroyAsset(instance.Gltf);
    } else {
        if (instance.AddedToScene) m_impl->Scene.remove(instance.Filamesh.renderable);
        m_impl->Engine.destroy(instance.Filamesh.renderable);
        m_impl->Engine.destroy(instance.Filamesh.vertexBuffer);
        m_impl->Engine.destroy(instance.Filamesh.indexBuffer);
        m_impl->Engine.getEntityManager().destroy(instance.Filamesh.renderable);
        for (auto* material : instance.MaterialInstances) m_impl->Engine.destroy(material);
    }
    m_impl->Instances.erase(found);
}

void RenderAssets::Update() {
    if (m_impl && m_impl->GltfTextures) m_impl->GltfTextures->updateQueue();
    if (m_impl && m_impl->StandaloneTextures) m_impl->StandaloneTextures->updateQueue();
}

void RenderAssets::Shutdown() {
    if (!m_impl) return;
    while (!m_impl->Instances.empty()) DestroyMesh(m_impl->Instances.begin()->first);
    for (const auto& [id, texture] : m_impl->Textures) m_impl->Engine.destroy(texture);
    for (const auto& [id, material] : m_impl->Materials) m_impl->Engine.destroy(material);
    m_impl->Textures.clear(); m_impl->Materials.clear();
    if (m_impl->GltfLoader) filament::gltfio::AssetLoader::destroy(&m_impl->GltfLoader);
    if (m_impl->GltfMaterials) { m_impl->GltfMaterials->destroyMaterials(); delete m_impl->GltfMaterials; }
    delete m_impl->GltfTextures; delete m_impl->StandaloneTextures;
    m_impl.reset();
}

} // namespace Bazzalt::Runtime
