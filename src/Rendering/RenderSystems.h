#pragma once

#include <unordered_map>
#include <filesystem>

#include <utils/Entity.h>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/System.h"
#include "Bazzalt/UUID.h"
#include "Rendering/RenderAssets.h"

namespace filament { class Camera; class View; class Texture; class RenderTarget; }
namespace Bazzalt::Runtime {

class RenderBackend;

class CameraSystem final : public ComponentSystem<Transform, Camera> {
public:
    explicit CameraSystem(RenderBackend& backend) : m_backend(backend) {}
    void Synchronize(Scene& scene){OnUpdate(scene,0);}
protected:
    void OnCreate(Scene& scene) override;
    void OnUpdate(Scene& scene, float deltaTime) override;
    void OnDestroy(Scene& scene) override;
private:
    struct Resource { utils::Entity Entity; filament::Camera* Camera = nullptr; filament::View* View = nullptr; filament::Texture* Color=nullptr;filament::Texture* Depth=nullptr;filament::RenderTarget* Target=nullptr; };
    void Destroy(UUID id);
    RenderBackend& m_backend;
    std::unordered_map<UUID, Resource> m_resources;
};

class LightSystem final : public ComponentSystem<Transform, Light> {
public:
    explicit LightSystem(RenderBackend& backend) : m_backend(backend) {}
protected:
    void OnCreate(Scene& scene) override;
    void OnUpdate(Scene& scene, float deltaTime) override;
    void OnDestroy(Scene& scene) override;
private:
    struct Resource { utils::Entity Entity; LightType Type = LightType::Point; };
    void Destroy(UUID id);
    RenderBackend& m_backend;
    std::unordered_map<UUID, Resource> m_resources;
};

class MeshSystem final : public ComponentSystem<Transform, Mesh> {
public:
    explicit MeshSystem(RenderBackend& backend) : m_backend(backend) {}
protected:
    void OnCreate(Scene& scene) override;
    void OnUpdate(Scene& scene, float deltaTime) override;
    void OnDestroy(Scene& scene) override;
private:
    struct Resource {
        RenderAssets::Handle Handle = RenderAssets::InvalidHandle;
        UUID MeshAsset{};
        UUID MaterialAsset{};
        std::vector<UUID> Materials;
        UUID ModelOwner{};
        std::uint32_t ModelNodeIndex = Mesh::EntireAsset;
        std::filesystem::path CachePath;
    };
    void Destroy(UUID id);
    RenderBackend& m_backend;
    std::unordered_map<UUID, Resource> m_resources;
};

class PrimitiveSystem final : public ComponentSystem<Transform, PrimitiveObject> {
public:
    explicit PrimitiveSystem(RenderBackend& backend) : m_backend(backend) {}
protected:
    void OnCreate(Scene&) override;
    void OnUpdate(Scene& scene, float deltaTime) override;
    void OnDestroy(Scene&) override;
private:
    struct Resource { RenderAssets::Handle Handle=RenderAssets::InvalidHandle;std::size_t GeometryKey=0; };
    void Destroy(UUID id);
    RenderBackend& m_backend;
    std::unordered_map<UUID,Resource> m_resources;
};

} // namespace Bazzalt::Runtime
