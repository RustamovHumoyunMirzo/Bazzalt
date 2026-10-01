#include "Rendering/RenderSystems.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Rendering/BuiltinPostProcess.h"
#include "Rendering/RenderBackend.h"

namespace Bazzalt::Runtime {
namespace {

filament::math::float3 ToFilament(Vec3 value) { return {value.X, value.Y, value.Z}; }

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

Vec3 SafeVector(Vec3 value, Vec3 fallback = {}) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z)
        ? value : fallback;
}

Vec3 SafeDirection(Vec3 value, Vec3 fallback) {
    value = SafeVector(value, fallback);
    return value.LengthSquared() > Epsilon ? value.Normalized() : fallback;
}

LightType SafeLightType(LightType type) {
    switch (type) {
    case LightType::Directional:
    case LightType::Sun:
    case LightType::Point:
    case LightType::Spot: return type;
    default: return LightType::Point;
    }
}

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
    m_backend.ClearPostProcessEffects();
    std::unordered_set<UUID> alive;
    struct Submission { int Priority; UUID Id; filament::View* View; };
    std::vector<Submission> submissions;

    auto view = GetView(scene.GetRegistry());
    for (const auto handle : view) {
        Entity entity = scene.GetEntity(static_cast<Entity::Id>(handle));
        const UUID id = entity.GetUUID();
        const auto& camera = view.get<Camera>(handle);
        if (!camera.IsEnabled()) continue;
        alive.insert(id);
        auto found = m_resources.find(id);
        if (found == m_resources.end()) {
            Resource resource;
            resource.Entity = engine.getEntityManager().create();
            resource.Camera = engine.createCamera(resource.Entity);
            resource.View = engine.createView();
            resource.View->setScene(&m_backend.GetScene());
            resource.View->setCamera(resource.Camera);
            // Runtime cameras must never render editor-only layer 0x80
            // (grid, transform gizmos, and other Scene-view helpers).
            resource.View->setVisibleLayers(0xff, 0x7f);
            found = m_resources.emplace(id, resource).first;
        }

        auto& resource = found->second;
        const Mat4 world = entity.GetWorldMatrix();
        const Vec3 position = SafeVector(world.TransformPoint({}));
        const Vec3 forward = SafeDirection(world.TransformDirection({0.0f, 0.0f, -1.0f}), {0,0,-1});
        Vec3 up = SafeDirection(world.TransformDirection({0.0f, 1.0f, 0.0f}), {0,1,0});
        if (std::fabs(Vec3::Dot(forward, up)) > 0.999f) up = {0,1,0};
        resource.Camera->lookAt(ToFilament(position), ToFilament(position + forward), ToFilament(up));
        const double nearPlane = std::max(0.001, static_cast<double>(FiniteOr(camera.NearPlane, 0.1f)));
        const double farPlane = std::max(nearPlane + 0.001, static_cast<double>(FiniteOr(camera.FarPlane, 1000.0f)));
        const float viewportX = std::clamp(FiniteOr(camera.Viewport.X, 0.0f), 0.0f, 1.0f);
        const float viewportY = std::clamp(FiniteOr(camera.Viewport.Y, 0.0f), 0.0f, 1.0f);
        const float viewportWidth = std::clamp(FiniteOr(camera.Viewport.Width, 1.0f),
                                               0.0f, 1.0f - viewportX);
        const float viewportHeight = std::clamp(FiniteOr(camera.Viewport.Height, 1.0f),
                                                0.0f, 1.0f - viewportY);
        const auto targetWidth = m_backend.GetPresentationWidth();
        const auto targetHeight = m_backend.GetPresentationHeight();
        const auto left = static_cast<std::int32_t>(std::lround(viewportX * targetWidth));
        const auto bottom = static_cast<std::int32_t>(std::lround(viewportY * targetHeight));
        const auto right = static_cast<std::int32_t>(std::lround(
            (viewportX + viewportWidth) * targetWidth));
        const auto top = static_cast<std::int32_t>(std::lround(
            (viewportY + viewportHeight) * targetHeight));
        const auto pixelWidth = static_cast<std::uint32_t>(std::max(0, right - left));
        const auto pixelHeight = static_cast<std::uint32_t>(std::max(0, top - bottom));
        resource.View->setViewport(filament::Viewport(left, bottom, pixelWidth, pixelHeight));
        const bool automaticAspect = camera.AspectMode != CameraAspectMode::Fixed;
        const double aspect = automaticAspect && pixelHeight > 0
            ? static_cast<double>(pixelWidth) / static_cast<double>(pixelHeight)
            : std::max(0.001, static_cast<double>(FiniteOr(camera.AspectRatio, 16.0f / 9.0f)));
        if (camera.Projection != CameraProjection::Orthographic) {
            const double fov = std::clamp(static_cast<double>(ToDegrees(FiniteOr(
                camera.VerticalFieldOfView, ToRadians(60.0f)))), 1.0, 179.0);
            resource.Camera->setProjection(fov, aspect, nearPlane, farPlane,
                filament::Camera::Fov::VERTICAL);
        } else {
            const double halfHeight = std::max(0.001, static_cast<double>(FiniteOr(
                camera.OrthographicSize, 10.0f)) * 0.5);
            const double halfWidth = halfHeight * aspect;
            resource.Camera->setProjection(filament::Camera::Projection::ORTHO,
                -halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
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
        const auto& depthOfField = camera.PostProcessing.DepthOfField;
        const auto* blur = entity.TryGetComponent<GaussianBlur>();
        const bool blurEnabled = camera.PostProcessing.Enabled && blur && blur->IsEnabled() &&
            FiniteOr(blur->Size, 0.0f) > 0.0f;
        const float blurRadius = blurEnabled
            ? std::clamp(FiniteOr(blur->Size, 1.0f), 0.0f, 32.0f) : 0.0f;
        const float focusDistance = blurEnabled ? 0.001f : std::max(0.001f,
            FiniteOr(depthOfField.FocusDistance, 10.0f));
        const float aperture = std::clamp(FiniteOr(depthOfField.Aperture, 16.0f), 0.5f, 64.0f);
        const float shutterSpeed = std::max(0.000001f,
            FiniteOr(depthOfField.ShutterSpeed, 1.0f / 125.0f));
        const float exposureCompensation = std::clamp(
            FiniteOr(camera.PostProcessing.Exposure, 0.0f), -16.0f, 16.0f);
        const float sensitivity = std::clamp(
            FiniteOr(depthOfField.Sensitivity, 100.0f) * std::exp2(exposureCompensation),
            10.0f, 204800.0f);
        resource.Camera->setFocusDistance(focusDistance);
        resource.Camera->setExposure(aperture, shutterSpeed, sensitivity);
        std::uint8_t ringCount = 5;
        if (depthOfField.Quality == DepthOfFieldQuality::Low) ringCount = 3;
        else if (depthOfField.Quality == DepthOfFieldQuality::High) ringCount = 7;
        resource.View->setDepthOfFieldOptions({
            .cocScale = blurEnabled ? std::max(1.0f, blurRadius) :
                std::max(0.0f, FiniteOr(depthOfField.CocScale, 1.0f)),
            .cocAspectRatio = std::max(0.01f, FiniteOr(depthOfField.CocAspectRatio, 1.0f)),
            .maxApertureDiameter = blurEnabled ? std::max(0.01f, blurRadius * 0.01f) :
                std::max(0.0f, FiniteOr(depthOfField.MaxApertureDiameter, 0.01f)),
            .enabled = camera.PostProcessing.Enabled && (depthOfField.Enabled || blurEnabled),
            .filter = filament::View::DepthOfFieldOptions::Filter::MEDIAN,
            .nativeResolution = depthOfField.NativeResolution,
            .foregroundRingCount = ringCount,
            .backgroundRingCount = ringCount,
            .fastGatherRingCount = ringCount,
            .maxForegroundCOC = static_cast<std::uint16_t>(blurEnabled ? blurRadius : 0.0f),
            .maxBackgroundCOC = static_cast<std::uint16_t>(blurEnabled ? blurRadius : 0.0f)
        });
        std::vector<CustomPostProcessEffect> customEffects;
        const auto* vignette = entity.TryGetComponent<Vignette>();
        const bool vignetteEnabled = camera.PostProcessing.Enabled && vignette && vignette->IsEnabled();
        resource.View->setVignetteOptions({
            .midPoint = vignetteEnabled ? 1.0f - std::clamp(FiniteOr(vignette->Intensity, .35f), 0.0f, 1.0f) : .5f,
            .roundness = vignetteEnabled ? std::clamp(FiniteOr(vignette->Roundness, 1.0f), 0.0f, 1.0f) : .5f,
            .feather = vignetteEnabled ? std::clamp(FiniteOr(vignette->Smoothness, .35f), .001f, 1.0f) : .5f,
            .color = vignetteEnabled ? filament::math::float4{vignette->Color.X,vignette->Color.Y,vignette->Color.Z,vignette->Color.W} : filament::math::float4{0,0,0,1},
            .enabled = vignetteEnabled
        });
        for (const auto& effect : camera.PostProcessing.CustomEffects.GetEffects())
            if (effect.Enabled && effect.ShaderAsset)
                customEffects.push_back(effect);
        std::stable_sort(customEffects.begin(), customEffects.end(),
            [](const CustomPostProcessEffect& left, const CustomPostProcessEffect& right) {
                return left.Order < right.Order;
            });
        customEffects.erase(std::remove_if(customEffects.begin(), customEffects.end(),
            [this](const CustomPostProcessEffect& effect) {
                return !m_backend.GetAssets().PreparePostProcessEffect(effect);
            }), customEffects.end());
        m_backend.SetPostProcessEffects(resource.View, std::move(customEffects));
        if (camera.Active && pixelWidth > 0 && pixelHeight > 0)
            submissions.push_back({camera.Priority, id, resource.View});
    }
    for (auto iterator = m_resources.begin(); iterator != m_resources.end();) {
        if (alive.find(iterator->first) == alive.end()) {
            const UUID id = iterator->first; ++iterator; Destroy(id);
        } else ++iterator;
    }
    std::sort(submissions.begin(), submissions.end(), [](const Submission& left,
                                                         const Submission& right) {
        return left.Priority != right.Priority ? left.Priority < right.Priority
                                               : left.Id < right.Id;
    });
    std::vector<filament::View*> activeViews;
    activeViews.reserve(submissions.size());
    for (const auto& submission : submissions) activeViews.push_back(submission.View);
    m_backend.SetActiveViews(std::move(activeViews));
}

void CameraSystem::OnDestroy(Scene&) {
    while (!m_resources.empty()) Destroy(m_resources.begin()->first);
    m_backend.SetActiveViews({});
    m_backend.ClearPostProcessEffects();
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
        const UUID id = entity.GetUUID();
        const auto& light = view.get<Light>(handle);
        if (!light.IsEnabled()) continue;
        alive.insert(id);
        const LightType type = SafeLightType(light.Type);
        const Vec3 color = SafeVector(light.Color, {1,1,1});
        const float intensity = std::max(0.0f, FiniteOr(light.Intensity, 0.0f));
        const float range = std::max(0.001f, FiniteOr(light.Range, 10.0f));
        const float outerCone = std::clamp(FiniteOr(light.OuterConeAngle, ToRadians(30.0f)),
                                           0.001f, Pi * 0.5f);
        const float innerCone = std::clamp(FiniteOr(light.InnerConeAngle, ToRadians(20.0f)),
                                           0.0f, outerCone);
        auto found = m_resources.find(id);
        if (found != m_resources.end() && found->second.Type != type) {
            Destroy(id);
            found = m_resources.end();
        }
        if (found == m_resources.end()) {
            utils::Entity resource = engine.getEntityManager().create();
            filament::LightManager::Builder builder(ToFilament(type));
            builder.color(ToFilament(color))
                   .intensity(intensity)
                   .castShadows(light.CastShadows);
            const Mat4 initialWorld = entity.GetWorldMatrix();
            if (type == LightType::Point || type == LightType::Spot)
                builder.position(ToFilament(SafeVector(initialWorld.TransformPoint({})))).falloff(range);
            if (type != LightType::Point)
                builder.direction(ToFilament(SafeDirection(initialWorld.TransformDirection(
                    {0.0f, 0.0f, -1.0f}), {0,0,-1})));
            if (type == LightType::Spot) builder.spotLightCone(innerCone, outerCone);
            if (type == LightType::Sun)
                builder.sunAngularRadius(std::clamp(ToDegrees(FiniteOr(light.SunAngularRadius, 0.00935f)), 0.1f, 20.0f))
                       .sunHaloSize(std::max(0.0f, FiniteOr(light.SunHaloSize, 10.0f)))
                       .sunHaloFalloff(std::max(0.0f, FiniteOr(light.SunHaloFalloff, 80.0f)));
            builder.build(engine, resource);
            m_backend.GetScene().addEntity(resource);
            found = m_resources.emplace(id, Resource{resource, type}).first;
        }
        const auto instance = manager.getInstance(found->second.Entity);
        manager.setColor(instance, ToFilament(color));
        manager.setIntensity(instance, intensity);
        const Mat4 world = entity.GetWorldMatrix();
        if (type == LightType::Point || type == LightType::Spot) {
            manager.setFalloff(instance, range);
            manager.setPosition(instance, ToFilament(SafeVector(world.TransformPoint({}))));
        }
        if (type != LightType::Point) {
            manager.setDirection(instance, ToFilament(SafeDirection(
                world.TransformDirection({0.0f, 0.0f, -1.0f}), {0,0,-1})));
        }
        if (type == LightType::Spot) manager.setSpotLightCone(instance, innerCone, outerCone);
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
    auto& assets = m_backend.GetAssets();
    assets.Update();
    std::unordered_set<UUID> alive;
    auto view = GetView(scene.GetRegistry());
    for (const auto handle : view) {
        Entity entity = scene.GetEntity(static_cast<Entity::Id>(handle));
        const UUID id = entity.GetUUID();
        const auto& mesh = view.get<Mesh>(handle);
        if (!mesh.IsEnabled()) continue;
        alive.insert(id);
        auto found = m_resources.find(id);
        if (found != m_resources.end() &&
            (found->second.MeshAsset != mesh.MeshAsset || found->second.Materials != mesh.Materials)) {
            Destroy(id); found = m_resources.end();
        }
        if (found == m_resources.end()) {
            const auto resource = assets.CreateMesh(mesh);
            found = m_resources.emplace(id, Resource{resource, mesh.MeshAsset, mesh.Materials}).first;
        }
        assets.UpdateMesh(found->second.Handle, entity.GetWorldMatrix(), mesh);
    }
    for (auto iterator=m_resources.begin();iterator!=m_resources.end();) {
        if (!alive.count(iterator->first)) { const UUID id=iterator->first; ++iterator; Destroy(id); }
        else ++iterator;
    }
}
void MeshSystem::OnDestroy(Scene&) { while(!m_resources.empty()) Destroy(m_resources.begin()->first); }
void MeshSystem::Destroy(UUID id) {
    auto found=m_resources.find(id);if(found==m_resources.end())return;
    m_backend.GetAssets().DestroyMesh(found->second.Handle);m_resources.erase(found);
}

namespace { std::size_t PrimitiveGeometryKey(const PrimitiveObject&v){std::size_t h=static_cast<std::size_t>(v.Shape);const auto mix=[&](auto value){h^=std::hash<decltype(value)>{}(value)+0x9e3779b9+(h<<6)+(h>>2);};mix(v.Size.X);mix(v.Size.Y);mix(v.Size.Z);mix(v.Radius);mix(v.Height);mix(v.Width);mix(v.Depth);mix(v.MajorRadius);mix(v.MinorRadius);mix(v.Segments);mix(v.Rings);return h;} }
void PrimitiveSystem::OnCreate(Scene&){}
void PrimitiveSystem::OnUpdate(Scene&scene,float){auto& assets=m_backend.GetAssets();std::unordered_set<UUID> alive;auto view=GetView(scene.GetRegistry());for(auto handle:view){Entity entity=scene.GetEntity(static_cast<Entity::Id>(handle));const auto id=entity.GetUUID();const auto& primitive=view.get<PrimitiveObject>(handle);if(!primitive.IsEnabled())continue;alive.insert(id);const auto key=PrimitiveGeometryKey(primitive);auto found=m_resources.find(id);if(found!=m_resources.end()&&found->second.GeometryKey!=key){Destroy(id);found=m_resources.end();}if(found==m_resources.end())found=m_resources.emplace(id,Resource{assets.CreatePrimitive(primitive),key}).first;assets.UpdatePrimitive(found->second.Handle,entity.GetWorldMatrix(),primitive);}for(auto it=m_resources.begin();it!=m_resources.end();){if(!alive.contains(it->first)){auto id=it->first;++it;Destroy(id);}else ++it;}}
void PrimitiveSystem::OnDestroy(Scene&){while(!m_resources.empty())Destroy(m_resources.begin()->first);}
void PrimitiveSystem::Destroy(UUID id){auto found=m_resources.find(id);if(found==m_resources.end())return;m_backend.GetAssets().DestroyPrimitive(found->second.Handle);m_resources.erase(found);}

} // namespace Bazzalt::Runtime
