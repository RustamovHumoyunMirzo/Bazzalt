#pragma once
#include <memory>
#include <cstdint>
#include "Bazzalt/UUID.h"
namespace filament {class View;}
namespace Bazzalt::Runtime {
class RenderBackend;
class GraphicsRenderer final {
public:
    explicit GraphicsRenderer(RenderBackend& backend);
    ~GraphicsRenderer();
    void BeginFrame();
    void AddCamera(filament::View*,UUID,std::uint64_t viewport,bool editor);
    void Prepare();
    void BeforeCamera(filament::View*,std::uint64_t viewport);
    void AfterCamera(filament::View*,std::uint64_t viewport);
    void EndFrame();
private:
    struct Impl;std::unique_ptr<Impl> m_impl;
};
}
