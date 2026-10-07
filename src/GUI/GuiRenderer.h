#pragma once
#include <memory>
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
namespace filament { class Engine;class Scene;class Renderer;class Texture;class RenderTarget; }
namespace Bazzalt { class Scene; }
namespace Bazzalt::Runtime {
class RenderBackend;
class GuiRenderer final {
public:
    explicit GuiRenderer(RenderBackend& backend);
    ~GuiRenderer();
    void PrepareSpatial(Bazzalt::Scene& scene);
    void PrepareOverlay(Bazzalt::Scene& scene,Vec2 size,UUID camera={},filament::RenderTarget* target=nullptr);
    void RenderOverlay(Bazzalt::Scene& scene,Vec2 size);
    void RenderCameraOverlay(Bazzalt::Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target);
    void Clear();
    [[nodiscard]] std::size_t GetBatchCount() const;
private:
    void* m_impl=nullptr;
};
}
