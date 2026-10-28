#pragma once
#include <memory>
#include <cstdint>
#include <array>
#include <unordered_set>
#include "Bazzalt/UUID.h"
namespace filament {class View;class Scene;class MaterialInstance;}
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
    std::array<std::uint64_t,2> GetResourceStatistics() const;
    bool BeginSelectionMask(filament::Scene&,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered,filament::MaterialInstance*,filament::MaterialInstance*);
    void EndSelectionMask();
private:
    struct Impl;std::unique_ptr<Impl> m_impl;
};
}
