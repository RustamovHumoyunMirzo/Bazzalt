#pragma once

#include <memory>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

#include "Bazzalt/PostProcessing.h"

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
    void SetActiveViews(std::vector<filament::View*> views) { m_activeViews = std::move(views); }
    [[nodiscard]] const std::vector<filament::View*>& GetActiveViews() const { return m_activeViews; }
    void SetPresentationSize(std::uint32_t width, std::uint32_t height) {
        m_presentationWidth = width > 0 ? width : 1;
        m_presentationHeight = height > 0 ? height : 1;
    }
    [[nodiscard]] std::uint32_t GetPresentationWidth() const { return m_presentationWidth; }
    [[nodiscard]] std::uint32_t GetPresentationHeight() const { return m_presentationHeight; }
    void ClearPostProcessEffects() { m_postProcessEffects.clear(); }
    void SetPostProcessEffects(filament::View* view,
                               std::vector<CustomPostProcessEffect> effects) {
        m_postProcessEffects[view] = std::move(effects);
    }
    [[nodiscard]] const std::vector<CustomPostProcessEffect>* GetPostProcessEffects(
        filament::View* view) const {
        const auto found = m_postProcessEffects.find(view);
        return found == m_postProcessEffects.end() ? nullptr : &found->second;
    }

private:
    filament::Engine* m_engine = nullptr;
    filament::Renderer* m_renderer = nullptr;
    filament::Scene* m_scene = nullptr;
    std::vector<filament::View*> m_activeViews;
    std::uint32_t m_presentationWidth = 1280;
    std::uint32_t m_presentationHeight = 720;
    std::unordered_map<filament::View*, std::vector<CustomPostProcessEffect>> m_postProcessEffects;
    std::unique_ptr<RenderAssets> m_assets;
};

} // namespace Bazzalt::Runtime
