#include "Rendering/FogRenderer.h"
#include "Rendering/LightParameters.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/Light.h"
#include "volumetric_fog_filamat.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/mat4.h>
#include <map>
#include <algorithm>
namespace Bazzalt::Runtime {
using ::operator|;
struct FogRenderer::Impl {
    filament::Engine &Engine;
    filament::Renderer &Renderer;
    filament::Material *Material = nullptr;
    filament::Scene *Scene = nullptr;
    filament::Skybox *ClearSky = nullptr;
    filament::View *View = nullptr;
    filament::View *DepthView = nullptr;
    filament::Camera *Camera = nullptr;
    utils::Entity CameraEntity, Quad;
    filament::VertexBuffer *Vertices = nullptr;
    filament::IndexBuffer *Indices = nullptr;
    struct Target {
        unsigned Width = 0, Height = 0;
        filament::Texture *Color = nullptr;
        filament::Texture *Depth = nullptr;
        filament::RenderTarget *Framebuffer = nullptr;
        filament::RenderTarget *ColorTarget = nullptr;
        filament::MaterialInstance *Material = nullptr;
    };
    std::map<filament::View *, Target> Targets;
    Impl(filament::Engine &engine, filament::Renderer &renderer) : Engine(engine), Renderer(renderer) {
        Material = filament::Material::Builder()
                       .package(Embedded::VolumetricFogFilamat, Embedded::VolumetricFogFilamatSize)
                       .build(Engine);
        if (!Material)
            throw std::runtime_error("Could not create volumetric fog material");
        Scene = Engine.createScene();
        View = Engine.createView();
        ClearSky = filament::Skybox::Builder().color({0, 0, 0, 1}).showSun(false).build(Engine);
        DepthView = Engine.createView();
        DepthView->setPostProcessingEnabled(false);
        DepthView->setShadowingEnabled(false);
        CameraEntity = Engine.getEntityManager().create();
        Camera = Engine.createCamera(CameraEntity);
        Camera->setProjection(filament::Camera::Projection::ORTHO, -1, 1, -1, 1, 0, 1);
        View->setScene(Scene);
        View->setCamera(Camera);
        View->setPostProcessingEnabled(false);
        View->setShadowingEnabled(false);
        Vertices = filament::VertexBuffer::Builder()
                       .vertexCount(3)
                       .bufferCount(1)
                       .attribute(filament::VertexAttribute::POSITION, 0,
                                  filament::VertexBuffer::AttributeType::FLOAT3)
                       .build(Engine);
        auto *positions = new float[9]{-1, -1, .5f, 3, -1, .5f, -1, 3, .5f};
        Vertices->setBufferAt(Engine, 0, {positions, sizeof(float) * 9, [](void *data, std::size_t, void *) {
                                              delete[] static_cast<float *>(data);
                                          }});
        Indices = filament::IndexBuffer::Builder()
                      .indexCount(3)
                      .bufferType(filament::IndexBuffer::IndexType::USHORT)
                      .build(Engine);
        auto *indices = new std::uint16_t[3]{0, 1, 2};
        Indices->setBuffer(Engine, {indices, sizeof(std::uint16_t) * 3, [](void *data, std::size_t, void *) {
                                        delete[] static_cast<std::uint16_t *>(data);
                                    }});
        Quad = Engine.getEntityManager().create();
        Engine.getTransformManager().create(Quad);
        filament::RenderableManager::Builder(1)
            .boundingBox({{0, 0, 0}, {3, 3, 1}})
            .material(0, Material->getDefaultInstance())
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, Vertices, Indices)
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(Engine, Quad);
        Scene->addEntity(Quad);
    }
    void Destroy(Target &target) {
        if (target.Material) {
            auto instance = Engine.getRenderableManager().getInstance(Quad);
            if (instance)
                Engine.getRenderableManager().setMaterialInstanceAt(instance, 0,
                                                                    Material->getDefaultInstance());
            Engine.destroy(target.Material);
        }
        if (target.Framebuffer)
            Engine.destroy(target.Framebuffer);
        if (target.ColorTarget)
            Engine.destroy(target.ColorTarget);
        if (target.Color)
            Engine.destroy(target.Color);
        if (target.Depth)
            Engine.destroy(target.Depth);
        target = {};
    }
    ~Impl() {
        for (auto &[_, target] : Targets)
            Destroy(target);
        Engine.destroy(DepthView);
        Engine.destroy(View);
        Engine.destroy(Quad);
        Engine.getEntityManager().destroy(Quad);
        Engine.destroy(Vertices);
        Engine.destroy(Indices);
        Engine.destroy(Scene);
        Engine.destroy(ClearSky);
        Engine.destroyCameraComponent(CameraEntity);
        Engine.getEntityManager().destroy(CameraEntity);
        Engine.destroy(Material);
    }
    static filament::math::mat4f Matrix(const Mat4 &matrix) {
        filament::math::mat4f result;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                result[c][r] = matrix(r, c);
        return result;
    }
    static Mat4 Matrix(const filament::math::mat4 &matrix) {
        Mat4 result;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                result(r, c) = float(matrix[c][r]);
        return result;
    }
    Target &Prepare(filament::View &input) {
        auto viewport = input.getViewport();
        auto &target = Targets[&input];
        if (target.Width == viewport.width && target.Height == viewport.height && target.Framebuffer)
            return target;
        Destroy(target);
        target.Width = viewport.width;
        target.Height = viewport.height;
        using T = filament::Texture;
        target.Color = T::Builder()
                           .width(target.Width)
                           .height(target.Height)
                           .levels(1)
                           .sampler(T::Sampler::SAMPLER_2D)
                           .format(T::InternalFormat::RGBA16F)
                           .usage(T::Usage::COLOR_ATTACHMENT | T::Usage::SAMPLEABLE)
                           .build(Engine);
        target.Depth = T::Builder()
                           .width(target.Width)
                           .height(target.Height)
                           .levels(1)
                           .sampler(T::Sampler::SAMPLER_2D)
                           .format(T::InternalFormat::DEPTH32F)
                           .usage(T::Usage::DEPTH_ATTACHMENT | T::Usage::SAMPLEABLE)
                           .build(Engine);
        target.Framebuffer = filament::RenderTarget::Builder()
                                 .texture(filament::RenderTarget::AttachmentPoint::COLOR, target.Color)
                                 .texture(filament::RenderTarget::AttachmentPoint::DEPTH, target.Depth)
                                 .build(Engine);
        target.ColorTarget = filament::RenderTarget::Builder()
                                 .texture(filament::RenderTarget::AttachmentPoint::COLOR, target.Color)
                                 .build(Engine);
        target.Material = Material->createInstance();
        const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::NEAREST,
                                               filament::TextureSampler::MagFilter::NEAREST);
        target.Material->setParameter("sceneColor", target.Color, sampler);
        target.Material->setParameter("sceneDepth", target.Depth, sampler);
        return target;
    }
};
FogRenderer::FogRenderer(filament::Engine &engine, filament::Renderer &renderer)
    : m_impl(std::make_unique<Impl>(engine, renderer)) {}
FogRenderer::~FogRenderer() = default;
void FogRenderer::Prepare(const std::vector<filament::View *> &views) {
    auto &impl = *m_impl;
    for (auto it = impl.Targets.begin(); it != impl.Targets.end();) {
        if (std::find(views.begin(), views.end(), it->first) == views.end()) {
            impl.Destroy(it->second);
            it = impl.Targets.erase(it);
        } else
            ++it;
    }
    for (auto *view : views)
        if (view && view->getViewport().width && view->getViewport().height)
            impl.Prepare(*view);
}
void FogRenderer::Render(filament::View &input, const FogSettings &fog, Bazzalt::Scene *scene, float time) {
    auto &impl = *m_impl;
    auto &target = impl.Prepare(input);
    auto viewport = input.getViewport();
    auto *original = input.getRenderTarget();
    const auto &camera = input.getCamera();
    auto projection = Impl::Matrix(camera.getProjectionMatrix());
    auto world = Impl::Matrix(camera.getModelMatrix());
    auto *material = target.Material;
    material->setParameter("cameraWorld", Impl::Matrix(world));
    material->setParameter("inverseProjection", Impl::Matrix(projection.Inversed()));
    material->setParameter("cameraInfo", filament::math::float4{camera.getNear(), camera.getCullingFar(),
                                                                std::abs(projection(3, 3)) > .5f ? 1.f : 0.f,
                                                                fog.AffectSky ? 1.f : 0.f});
    material->setParameter("fogInfo", filament::math::float4{fog.Density, fog.StartDistance, fog.MaxDistance,
                                                             fog.MaximumOpacity});
    material->setParameter("heightInfo",
                           filament::math::float4{fog.Height, fog.HeightFalloff, 0, float(fog.Steps)});
    material->setParameter("noiseInfo",
                           filament::math::float4{fog.NoiseAmount, fog.NoiseScale, fog.Anisotropy, time});
    material->setParameter("fogColor", filament::math::float3{fog.Color.X, fog.Color.Y, fog.Color.Z});
    material->setParameter("wind", filament::math::float3{fog.Wind.X, fog.Wind.Y, fog.Wind.Z});
    Vec3 direction{0, -1, 0}, color{};
    float intensity = 0;
    if (scene)
        for (auto handle : scene->GetRegistry().view<Light>()) {
            const auto light = SanitizeLight(scene->GetRegistry().get<Light>(handle));
            if (!light.Enabled || (light.Type != LightType::Directional && light.Type != LightType::Sun) ||
                light.Intensity <= intensity)
                continue;
            auto entity = scene->GetEntity(static_cast<Entity::Id>(handle));
            direction = entity.GetWorldMatrix().TransformDirection({0, 0, -1}).Normalized();
            intensity = light.Intensity;
            color = light.Color * (std::min(10.f, intensity / 100000.f));
        }
    material->setParameter("sunDirection", filament::math::float3{direction.X, direction.Y, direction.Z});
    material->setParameter("sunColor", filament::math::float3{color.X, color.Y, color.Z});
    const auto clear = impl.Renderer.getClearOptions();
    auto *sourceScene = input.getScene();
    auto *sky = sourceScene ? sourceScene->getSkybox() : nullptr;
    struct SkyScope {
        filament::Scene *Scene;
        filament::Skybox *Sky;
        ~SkyScope() {
            if (Scene)
                Scene->setSkybox(Sky);
        }
    } skyScope{sourceScene, sky};
    if (sourceScene && !sky) {
        impl.ClearSky->setColor({float(clear.clearColor.r), float(clear.clearColor.g),
                                 float(clear.clearColor.b), float(clear.clearColor.a)});
        sourceScene->setSkybox(impl.ClearSky);
    }
    auto sourceClear = clear;
    sourceClear.clear = true;
    impl.Renderer.setClearOptions(sourceClear);
    // Filament does not export a View's depth attachment when post-processing
    // is enabled. Populate it through an unprocessed view of the same geometry.
    if (input.isPostProcessingEnabled()) {
        impl.DepthView->setScene(input.getScene());
        impl.DepthView->setCamera(const_cast<filament::Camera *>(&camera));
        impl.DepthView->setVisibleLayers(0xff, input.getVisibleLayers());
        impl.DepthView->setFrustumCullingEnabled(input.isFrustumCullingEnabled());
        for (unsigned index = 0; index < 4; ++index)
            impl.DepthView->setMaterialGlobal(index, input.getMaterialGlobal(index));
        impl.DepthView->setRenderTarget(target.Framebuffer);
        impl.DepthView->setViewport({0, 0, target.Width, target.Height});
        try {
            impl.Renderer.render(impl.DepthView);
        } catch (...) {
            impl.DepthView->setRenderTarget(nullptr);
            impl.DepthView->setCamera(nullptr);
            impl.DepthView->setScene(nullptr);
            impl.Renderer.setClearOptions(clear);
            throw;
        }
        impl.DepthView->setRenderTarget(nullptr);
        impl.DepthView->setCamera(nullptr);
        impl.DepthView->setScene(nullptr);
    }
    input.setRenderTarget(input.isPostProcessingEnabled() ? target.ColorTarget : target.Framebuffer);
    input.setViewport({0, 0, target.Width, target.Height});
    try {
        impl.Renderer.render(&input);
    } catch (...) {
        input.setRenderTarget(original);
        input.setViewport(viewport);
        impl.Renderer.setClearOptions(clear);
        throw;
    }
    input.setRenderTarget(original);
    input.setViewport(viewport);
    impl.Renderer.setClearOptions(clear);
    impl.View->setBlendMode(input.getBlendMode());
    auto instance = impl.Engine.getRenderableManager().getInstance(impl.Quad);
    impl.Engine.getRenderableManager().setMaterialInstanceAt(instance, 0, material);
    impl.View->setRenderTarget(const_cast<filament::RenderTarget *>(original));
    impl.View->setViewport(viewport);
    try {
        impl.Renderer.render(impl.View);
    } catch (...) {
        impl.View->setRenderTarget(nullptr);
        throw;
    }
    impl.View->setRenderTarget(nullptr);
}
} // namespace Bazzalt::Runtime
