#include "Rendering/RenderBackend.h"

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include "Rendering/RenderAssets.h"

namespace Bazzalt::Runtime {

RenderBackend::~RenderBackend() { Shutdown(); }

bool RenderBackend::Initialize() {
    if (m_engine != nullptr) return true;
    m_engine = filament::Engine::create();
    if (m_engine == nullptr) return false;
    m_renderer = m_engine->createRenderer();
    m_scene = m_engine->createScene();
    if (m_renderer == nullptr || m_scene == nullptr) { Shutdown(); return false; }
    m_assets = std::make_unique<RenderAssets>(*m_engine, *m_scene);
    return true;
}

void RenderBackend::Shutdown() {
    if (m_engine == nullptr) return;
    m_activeView = nullptr;
    m_assets.reset();
    if (m_scene != nullptr) m_engine->destroy(m_scene);
    if (m_renderer != nullptr) m_engine->destroy(m_renderer);
    m_scene = nullptr;
    m_renderer = nullptr;
    filament::Engine::destroy(&m_engine);
}

} // namespace Bazzalt::Runtime
