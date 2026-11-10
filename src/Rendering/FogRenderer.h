#pragma once
#include <memory>
#include <vector>
#include "Bazzalt/Fog.h"
namespace filament {class Engine;class Renderer;class View;}
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
class FogRenderer {
public:
    FogRenderer(filament::Engine& engine,filament::Renderer& renderer);
    ~FogRenderer();
    void Prepare(const std::vector<filament::View*>& views);
    void Render(filament::View& view,const FogSettings& fog,Scene* scene,float time);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
