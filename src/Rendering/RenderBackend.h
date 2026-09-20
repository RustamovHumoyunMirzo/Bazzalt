#pragma once

#include <memory>

namespace filament {
class Engine;
class Renderer;
class Scene;
class View;
}

namespace Bazzalt::Runtime {

class RenderAssets;

// Owns Filament's low-level objects. No Filament type crosses the public API.
class RenderBackend final {
public:
    RenderBackend() = default;
    ~RenderBackend();
    RenderBackend(const RenderBackend&) = delete;
    RenderBackend& operator=(const RenderBackend&) = delete;

    bool Initialize();
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const { return m_engine != nullptr; }

    [[nodiscard]] filament::Engine& GetEngine() const { return *m_engine; }
    [[nodiscard]] filament::Renderer& GetRenderer() const { return *m_renderer; }
    [[nodiscard]] filament::Scene& GetScene() const { return *m_scene; }
    [[nodiscard]] RenderAssets& GetAssets() const { return *m_assets; }
    void SetActiveView(filament::View* view) { m_activeView = view; }
    [[nodiscard]] filament::View* GetActiveView() const { return m_activeView; }

private:
    filament::Engine* m_engine = nullptr;
    filament::Renderer* m_renderer = nullptr;
    filament::Scene* m_scene = nullptr;
    filament::View* m_activeView = nullptr;
    std::unique_ptr<RenderAssets> m_assets;
};

} // namespace Bazzalt::Runtime
