#include "Rendering/RenderSystems.h"

#include <limits>
#include <unordered_set>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/View.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include "Bazzalt/Scene.h"
#include "Rendering/RenderBackend.h"

namespace Bazzalt::Runtime {
namespace {

filament::math::float3 ToFilament(Vec3 value) { return {value.X, value.Y, value.Z}; }

filament::math::mat4f ToFilament(const Mat4& value) {
    return {filament::math::float4{value(0,0), value(1,0), value(2,0), value(3,0)},
            filament::math::float4{value(0,1), value(1,1), value(2,1), value(3,1)},
            filament::math::float4{value(0,2), value(1,2), value(2,2), value(3,2)},
            filament::math::float4{value(0,3), value(1,3), value(2,3), value(3,3)}};
}

filament::LightManager::Type ToFilament(LightType type) {
    using FilamentType = filament::LightManager::Type;
    switch (type) {
    case LightType::Directional: return FilamentType::DIRECTIONAL;
    case LightType::Sun: return FilamentType::SUN;
    case LightType::Spot: return FilamentType::SPOT;
    case LightType::Point: default: return FilamentType::POINT;
    }
}

} // namespace

void CameraSystem::OnCreate(Scene&) {}

void CameraSystem::OnUpdate(Scene& scene, float) {
    auto& engine = m_backend.GetEngine();
    std::unordered_set<UUID> alive;
    filament::View* activeView = nullptr;
    int activePriority = std::numeric_limits<int>::min();

    auto view = GetView(scene.GetRegistry());
    for (const auto handle : view) {
        Entity entity = scene.GetEntity(static_cast<Entity::Id>(handle));
        const UUID id = entity.GetUUID();
        alive.insert(id);
        auto found = m_resources.find(id);
        if (found == m_resources.end()) {
            Resource resource;
            resource.Entity = engine.getEntityManager().create();
            resource.Camera = engine.createCamera(resource.Entity);
            resource.View = engine.createView();
            resource.View->setScene(&m_backend.GetScene());
            resource.View->setCamera(resource.Camera);
            found = m_resources.emplace(id, resource).first;
        }

        const auto& camera = view.get<Camera>(handle);
        auto& resource = found->second;
        const Mat4 world = entity.GetWorldMatrix();
        const Vec3 position = world.TransformPoint({});
        const Vec3 forward = world.TransformDirection({0.0f, 0.0f, -1.0f}).Normalized();
        const Vec3 up = world.TransformDirection({0.0f, 1.0f, 0.0f}).Normalized();
        resource.Camera->lookAt(ToFilament(position), ToFilament(position + forward), ToFilament(up));
        if (camera.Projection == CameraProjection::Perspective) {
            resource.Camera->setProjection(ToDegrees(camera.VerticalFieldOfView), camera.AspectRatio,
                camera.NearPlane, camera.FarPlane, filament::Camera::Fov::VERTICAL);
        } else {
            const double halfHeight = camera.OrthographicSize * 0.5;
            const double halfWidth = halfHeight * camera.AspectRatio;
            resource.Camera->setProjection(filament::Camera::Projection::ORTHO,
                -halfWidth, halfWidth, -halfHeight, halfHeight, camera.NearPlane, camera.FarPlane);
        }
        resource.View->setPostProcessingEnabled(camera.PostProcessing.Enabled);
        resource.View->setAntiAliasing(camera.PostProcessing.AntiAliasingMode == AntiAliasing::None
            || camera.PostProcessing.AntiAliasingMode == AntiAliasing::TAA
            ? filament::View::AntiAliasing::NONE : filament::View::AntiAliasing::FXAA);
        resource.View->setTemporalAntiAliasingOptions({
            .enabled = camera.PostProcessing.Enabled &&
                       camera.PostProcessing.AntiAliasingMode == AntiAliasing::TAA
        });
        resource.View->setAmbientOcclusionOptions({
            .enabled = camera.PostProcessing.Enabled && camera.PostProcessing.AmbientOcclusion
        });
        resource.View->setBloomOptions({
            .enabled = camera.PostProcessing.Enabled && camera.PostProcessing.Bloom
        });
        if (camera.Active && camera.Priority >= activePriority) {
            activePriority = camera.Priority;
            activeView = resource.View;
        }
    }
    for (auto iterator = m_resources.begin(); iterator != m_resources.end();) {
        if (alive.find(iterator->first) == alive.end()) {
            const UUID id = iterator->first; ++iterator; Destroy(id);
        } else ++iterator;
    }
    m_backend.SetActiveView(activeView);
}

void CameraSystem::OnDestroy(Scene&) {
    while (!m_resources.empty()) Destroy(m_resources.begin()->first);
    m_backend.SetActiveView(nullptr);
}

void CameraSystem::Destroy(UUID id) {
    auto found = m_resources.find(id); if (found == m_resources.end()) return;
    auto& engine = m_backend.GetEngine();
    if (found->second.View) engine.destroy(found->second.View);
    engine.destroyCameraComponent(found->second.Entity);
    engine.getEntityManager().destroy(found->second.Entity);
    m_resources.erase(found);
}

void LightSystem::OnCreate(Scene&) {}

void LightSystem::OnUpdate(Scene& scene, float) {
    auto& engine = m_backend.GetEngine();
    auto& manager = engine.getLightManager();
    std::unordered_set<UUID> alive;
    auto view = GetView(scene.GetRegistry());
    for (const auto handle : view) {
        Entity entity = scene.GetEntity(static_cast<Entity::Id>(handle));
        const UUID id = entity.GetUUID(); alive.insert(id);
        const auto& light = view.get<Light>(handle);
        auto found = m_resources.find(id);
        if (found != m_resources.end() && found->second.Type != light.Type) {
            Destroy(id);
            found = m_resources.end();
        }
        if (found == m_resources.end()) {
            utils::Entity resource = engine.getEntityManager().create();
            filament::LightManager::Builder builder(ToFilament(light.Type));
            builder.color(ToFilament(light.Color))
                   .intensity(light.Enabled ? light.Intensity : 0.0f)
                   .castShadows(light.CastShadows);
            const Mat4 initialWorld = entity.GetWorldMatrix();
            if (light.Type == LightType::Point || light.Type == LightType::Spot)
                builder.position(ToFilament(initialWorld.TransformPoint({}))).falloff(light.Range);
            if (light.Type != LightType::Point)
                builder.direction(ToFilament(initialWorld.TransformDirection(
                    {0.0f, 0.0f, -1.0f}).Normalized()));
            if (light.Type == LightType::Spot)
                builder.spotLightCone(light.InnerConeAngle, light.OuterConeAngle);
            if (light.Type == LightType::Sun)
                builder.sunAngularRadius(ToDegrees(light.SunAngularRadius))
                       .sunHaloSize(light.SunHaloSize).sunHaloFalloff(light.SunHaloFalloff);
            builder.build(engine, resource);
            m_backend.GetScene().addEntity(resource);
            found = m_resources.emplace(id, Resource{resource, light.Type}).first;
        }
        const auto instance = manager.getInstance(found->second.Entity);
        manager.setColor(instance, ToFilament(light.Color));
        manager.setIntensity(instance, light.Enabled ? light.Intensity : 0.0f);
        const Mat4 world = entity.GetWorldMatrix();
        if (light.Type == LightType::Point || light.Type == LightType::Spot) {
            manager.setFalloff(instance, light.Range);
            manager.setPosition(instance, ToFilament(world.TransformPoint({})));
        }
        if (light.Type != LightType::Point) {
            manager.setDirection(instance, ToFilament(
                world.TransformDirection({0.0f, 0.0f, -1.0f}).Normalized()));
        }
        if (light.Type == LightType::Spot)
            manager.setSpotLightCone(instance, light.InnerConeAngle, light.OuterConeAngle);
        manager.setShadowCaster(instance, light.CastShadows);
    }
    for (auto iterator = m_resources.begin(); iterator != m_resources.end();) {
        if (!alive.count(iterator->first)) { const UUID id=iterator->first; ++iterator; Destroy(id); }
        else ++iterator;
    }
}

void LightSystem::OnDestroy(Scene&) { while (!m_resources.empty()) Destroy(m_resources.begin()->first); }

void LightSystem::Destroy(UUID id) {
    auto found=m_resources.find(id); if(found==m_resources.end())return;
    auto& engine=m_backend.GetEngine(); m_backend.GetScene().remove(found->second.Entity);
    engine.destroy(found->second.Entity); engine.getEntityManager().destroy(found->second.Entity); m_resources.erase(found);
}

void MeshSystem::OnCreate(Scene&) {}
void MeshSystem::OnUpdate(Scene& scene, float) {
    auto& engine = m_backend.GetEngine();
    auto& transforms = engine.getTransformManager();
    std::unordered_set<UUID> alive;
    auto view = GetView(scene.GetRegistry());
    for (const auto handle : view) {
        Entity entity = scene.GetEntity(static_cast<Entity::Id>(handle));
        const UUID id = entity.GetUUID(); alive.insert(id);
        auto found = m_resources.find(id);
        if (found == m_resources.end()) {
            const utils::Entity resource = engine.getEntityManager().create();
            transforms.create(resource);
            found = m_resources.emplace(id, resource).first;
        }
        transforms.setTransform(transforms.getInstance(found->second),
                                ToFilament(entity.GetWorldMatrix()));
        // Geometry and material attachment occurs when the mesh importer starts
        // producing Filament-ready cache artifacts.
    }
    for (auto iterator=m_resources.begin();iterator!=m_resources.end();) {
        if (!alive.count(iterator->first)) { const UUID id=iterator->first; ++iterator; Destroy(id); }
        else ++iterator;
    }
}
void MeshSystem::OnDestroy(Scene&) { while(!m_resources.empty()) Destroy(m_resources.begin()->first); }
void MeshSystem::Destroy(UUID id) {
    auto found=m_resources.find(id);if(found==m_resources.end())return;
    auto& engine=m_backend.GetEngine();engine.destroy(found->second);
    engine.getEntityManager().destroy(found->second);m_resources.erase(found);
}

} // namespace Bazzalt::Runtime
