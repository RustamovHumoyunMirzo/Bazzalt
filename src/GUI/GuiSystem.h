#pragma once
#include <unordered_map>
#include "Bazzalt/GUI.h"
#include "Bazzalt/Scene.h"
#include "GUI/CSS/CSS.h"
#include "GUI/GuiPaint.h"

namespace Bazzalt::Runtime {
struct GuiComputed {
    RectTransform Rect;GuiStyle Style;GuiText Text;GuiImage Image;GuiButton Button;GuiTextInput Input;
    bool Hidden=false,PointerEvents=true;
    UUID BackgroundTexture{};
    GuiPaint Paint;
    CSS::ComputedStyle Cascade;CSS::Environment Context;
};
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
    std::unordered_map<UUID,GuiComputed> Computed;
    std::unordered_map<UUID,CSS::MotionState> Motion;
    std::vector<std::string> StyleDiagnostics;
    std::vector<std::string> ReportedStyleDiagnostics;
    void PrepareStyles(Entity root,GuiBounds surface,float scale);
protected:
    void OnUpdate(Scene& scene,float) override { Layout(scene,PresentationSize);ProcessInput(scene); }
private:
    void Descend(Entity parent,Entity root,GuiBounds bounds,GuiBounds clip,GuiBounds surface,float scale,std::size_t depth);
};
void ApplyGuiLayout(GuiComputed& value,Vec2 parentSize);
template<class T> const T* EffectiveGui(Entity entity){
    const auto* source=entity.TryGetComponent<T>();if(!source)return nullptr;
    auto* scene=entity.GetScene();if(!scene||!scene->HasSystem<GuiSystem>())return source;
    const auto& computed=scene->GetSystem<GuiSystem>().Computed;auto found=computed.find(entity.GetUUID());if(found==computed.end())return source;
    if constexpr(std::is_same_v<T,RectTransform>)return &found->second.Rect;
    else if constexpr(std::is_same_v<T,GuiButton>)return &found->second.Button;
    else if constexpr(std::is_same_v<T,GuiTextInput>)return &found->second.Input;
    else if constexpr(std::is_same_v<T,GuiImage>)return &found->second.Image;
    else return source;
}
}
