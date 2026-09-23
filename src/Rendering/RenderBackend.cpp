#include "Rendering/RenderBackend.h"

#include <algorithm>

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/IndirectLight.h>
#include <filament/Texture.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>
#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>
#include "Rendering/RenderAssets.h"
#include "default_environment_ibl.h"
#include "default_environment_skybox.h"

namespace Bazzalt::Runtime {

struct RenderBackend::ViewportResource {
    ViewportKind Kind = ViewportKind::Game;
    filament::SwapChain* SwapChain = nullptr;
    filament::View* View = nullptr;
    filament::Camera* Camera = nullptr;
    utils::Entity CameraEntity;
    std::uint32_t Width = 1;
    std::uint32_t Height = 1;
};

RenderBackend::RenderBackend() = default;

RenderBackend::~RenderBackend() { Shutdown(); }

bool RenderBackend::Initialize() {
    if (m_engine != nullptr) return true;
    m_engine = filament::Engine::create();
    if (m_engine == nullptr) return false;
    m_renderer = m_engine->createRenderer();
    m_scene = m_engine->createScene();
    if (m_renderer == nullptr || m_scene == nullptr) { Shutdown(); return false; }
    m_renderer->setClearOptions({.clearColor = {0.055, 0.065, 0.085, 1.0}, .clear = true});
    auto* iblBundle = new image::Ktx1Bundle(Embedded::DefaultEnvironmentIbl,
                                            Embedded::DefaultEnvironmentIblSize);
    auto* skyboxBundle = new image::Ktx1Bundle(Embedded::DefaultEnvironmentSkybox,
                                               Embedded::DefaultEnvironmentSkyboxSize);
    m_environmentIblTexture = ktxreader::Ktx1Reader::createTexture(m_engine, iblBundle, false);
    m_environmentSkyboxTexture = ktxreader::Ktx1Reader::createTexture(m_engine, skyboxBundle, false);
    if (m_environmentIblTexture && m_environmentSkyboxTexture) {
        m_indirectLight = filament::IndirectLight::Builder()
            .reflections(m_environmentIblTexture).intensity(30000.0f).build(*m_engine);
        m_skybox = filament::Skybox::Builder()
            .environment(m_environmentSkyboxTexture).intensity(30000.0f).showSun(true).build(*m_engine);
        m_scene->setIndirectLight(m_indirectLight);
        m_scene->setSkybox(m_skybox);
    }
    m_assets = std::make_unique<RenderAssets>(*m_engine, *m_scene);
    return true;
}

bool RenderBackend::CreateViewport(std::uint64_t id, std::uintptr_t nativeWindow,
                                   ViewportKind kind, std::uint32_t width,
                                   std::uint32_t height) {
    if (!m_engine || nativeWindow == 0) return false;
    DestroyViewport(id);
    auto viewport = std::make_unique<ViewportResource>();
    viewport->Kind = kind;
    viewport->Width = std::max(1u, width);
    viewport->Height = std::max(1u, height);
    viewport->SwapChain = m_engine->createSwapChain(reinterpret_cast<void*>(nativeWindow));
    if (!viewport->SwapChain) return false;
    viewport->CameraEntity = m_engine->getEntityManager().create();
    viewport->Camera = m_engine->createCamera(viewport->CameraEntity);
    viewport->View = m_engine->createView();
    viewport->View->setScene(m_scene);
    viewport->View->setCamera(viewport->Camera);
    viewport->View->setPostProcessingEnabled(true);
    if (kind == ViewportKind::Scene) {
        viewport->Camera->lookAt({6.0, 4.0, 8.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    } else {
        viewport->Camera->lookAt({0.0, 2.0, 6.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0});
    }
    m_viewports.emplace(id, std::move(viewport));
    ResizeViewport(id, width, height);
    return true;
}

void RenderBackend::ResizeViewport(std::uint64_t id, std::uint32_t width,
                                   std::uint32_t height) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    viewport.Width = std::max(1u, width);
    viewport.Height = std::max(1u, height);
    if (viewport.View) {
        viewport.View->setViewport({0, 0, viewport.Width, viewport.Height});
        viewport.Camera->setProjection(60.0,
            static_cast<double>(viewport.Width) / viewport.Height, 0.05, 5000.0,
            filament::Camera::Fov::VERTICAL);
    }
    if (viewport.Kind == ViewportKind::Game)
        SetPresentationSize(viewport.Width, viewport.Height);
}

void RenderBackend::DestroyViewport(std::uint64_t id) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end()) return;
    auto& viewport = *found->second;
    if (viewport.View) m_engine->destroy(viewport.View);
    if (viewport.Camera) m_engine->destroyCameraComponent(viewport.CameraEntity);
    if (viewport.CameraEntity) m_engine->getEntityManager().destroy(viewport.CameraEntity);
    if (viewport.SwapChain) m_engine->destroy(viewport.SwapChain);
    m_viewports.erase(found);
}

void RenderBackend::SetSceneCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                                   float targetX, float targetY, float targetZ) {
    const auto found = m_viewports.find(id);
    if (found == m_viewports.end() || !found->second->Camera) return;
    found->second->Camera->lookAt({eyeX, eyeY, eyeZ}, {targetX, targetY, targetZ},
                                  {0.0, 1.0, 0.0});
}

void RenderBackend::Render() {
    if (!m_renderer) return;
    for (const auto& [id, resource] : m_viewports) {
        (void)id;
        if (!resource->SwapChain || !m_renderer->beginFrame(resource->SwapChain)) continue;
        if (resource->Kind == ViewportKind::Scene) {
            if (resource->View) m_renderer->render(resource->View);
        } else {
            if (m_activeViews.empty()) {
                if (resource->View) m_renderer->render(resource->View);
            } else {
                for (filament::View* view : m_activeViews)
                    if (view) m_renderer->render(view);
            }
        }
        m_renderer->endFrame();
    }
}

void RenderBackend::Shutdown() {
    if (m_engine == nullptr) return;
    while (!m_viewports.empty()) DestroyViewport(m_viewports.begin()->first);
    m_activeViews.clear();
    m_postProcessEffects.clear();
    m_assets.reset();
    if (m_scene) { m_scene->setSkybox(nullptr); m_scene->setIndirectLight(nullptr); }
    if (m_skybox) m_engine->destroy(m_skybox);
    if (m_indirectLight) m_engine->destroy(m_indirectLight);
    if (m_environmentSkyboxTexture) m_engine->destroy(m_environmentSkyboxTexture);
    if (m_environmentIblTexture) m_engine->destroy(m_environmentIblTexture);
    m_skybox = nullptr; m_indirectLight = nullptr;
    m_environmentSkyboxTexture = nullptr; m_environmentIblTexture = nullptr;
    if (m_scene != nullptr) m_engine->destroy(m_scene);
    if (m_renderer != nullptr) m_engine->destroy(m_renderer);
    m_scene = nullptr;
    m_renderer = nullptr;
    filament::Engine::destroy(&m_engine);
}

} // namespace Bazzalt::Runtime
