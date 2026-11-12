#pragma once
#include <memory>
#include <array>
#include <cstdint>
#include <vector>
#include <unordered_set>
#include "Bazzalt/UUID.h"
#include "Bazzalt/Fog.h"
namespace filament {class Engine;class Renderer;class View;}
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
class FogRenderer {
public:
    FogRenderer(filament::Engine& engine,filament::Renderer& renderer);
    ~FogRenderer();
    struct Viewport {filament::View* View;unsigned Width,Height;};
    void Prepare(const std::vector<filament::View*>& views,std::uint64_t materialRevision=0,const std::vector<Viewport>& presentations={},Scene* scene=nullptr,const FogSettings* settings=nullptr);
    std::array<std::uint64_t,5> GetResourceCounts() const;
    void Render(filament::View& view,const FogSettings& fog,Scene* scene,float time,filament::Renderer* renderer=nullptr,const std::unordered_set<UUID>* hidden=nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
