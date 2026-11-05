#pragma once
#include "Bazzalt/Scene.h"
#include "GUI/GuiRenderer.h"
namespace Bazzalt::Runtime {
class DeveloperConsoleOverlay final {
public:
    explicit DeveloperConsoleOverlay(RenderBackend& backend);
    bool ProcessInput(Vec2 size);
    void Prepare(Vec2 size);
    void Render(Vec2 size);
private:
    void Refresh(Vec2 size);
    Scene m_scene;
    GuiRenderer m_renderer;
    Entity m_frame,m_output,m_input,m_title;
    int m_history=-1,m_scroll=0;
    bool m_visible=false;
    std::string m_draft;
};
}
