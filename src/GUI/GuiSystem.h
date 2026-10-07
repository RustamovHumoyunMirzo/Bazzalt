#pragma once
#include <unordered_map>
#include "Bazzalt/GUI.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt::Runtime {
struct GuiItem {
    Entity Owner,Root;
    GuiBounds Bounds,Clip;
    GuiBounds Surface;
    float Scale=1;
    std::size_t Order=0;
};
class GuiSystem final : public System {
public:
    void Layout(Scene& scene,Vec2 size);
    void ProcessInput(Scene& scene);
    bool Focus(Scene& scene,Entity entity);
    // Private module entry points; the core wrappers dispatch through GuiModuleApi.
    void ModuleLayout(Scene& scene,Vec2 size);
    void ModuleProcessInput(Scene& scene);
    bool ModuleFocus(Scene& scene,Entity entity);
    std::vector<GuiItem> Items;
    std::unordered_map<UUID,GuiInteraction> Interactions;
    UUID Hovered{},Focused{},Captured{};
    Vec2 PresentationSize{1280,720};
protected:
    void OnUpdate(Scene& scene,float) override { Layout(scene,PresentationSize);ProcessInput(scene); }
private:
    void Descend(Entity parent,Entity root,GuiBounds bounds,GuiBounds clip,GuiBounds surface,float scale,std::size_t depth);
};
}
