#pragma once
#include <memory>
#include "GUI/GuiRenderer.h"
namespace Bazzalt::Runtime {
// Implemented only in bazzalt_gui. No GPU implementation is linked into core.
class GuiRendererModule final {
public:
    explicit GuiRendererModule(RenderBackend& backend);
    ~GuiRendererModule();
    void PrepareSpatial(Bazzalt::Scene& scene);
    void PrepareOverlay(Bazzalt::Scene& scene,Vec2 size,UUID camera,filament::RenderTarget* target);
    void RenderOverlay(Bazzalt::Scene& scene,Vec2 size);
    void RenderCameraOverlay(Bazzalt::Scene& scene,UUID camera,Vec2 size,filament::RenderTarget* target);
    void Clear();
    std::size_t GetBatchCount() const;
private:
    struct Impl;std::unique_ptr<Impl> m_impl;
};
}
