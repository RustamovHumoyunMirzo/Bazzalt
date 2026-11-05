#include "Rendering/DeveloperConsoleOverlay.h"
#include "Bazzalt/DeveloperConsole.h"
#include "Bazzalt/Application.h"
#include "Bazzalt/Input.h"
#include "GUI/GuiSystem.h"
#include "Runtime/InputAccess.h"
#include <algorithm>
#include <cmath>
namespace Bazzalt::Runtime {
DeveloperConsoleOverlay::DeveloperConsoleOverlay(RenderBackend& backend):m_renderer(backend){
    m_frame=m_scene.CreateEntity("Developer console");auto& frame=m_frame.AddComponent<Frame>();frame.ScaleMode=GuiScaleMode::ConstantPixels;frame.Visible=false;frame.ZIndex=100000;
    auto panel=m_scene.CreateEntity("Console panel");panel.SetParent(m_frame,false);auto& rect=panel.AddComponent<RectTransform>();rect.AnchorMax={1,.5f};rect.Position={8,8};rect.Size={-16,-16};rect.ClipChildren=true;panel.AddComponent<Rectangle>().Style.Color={.025f,.03f,.04f,.97f};
    const auto text=[&](const char* name,Vec2 position,Vec2 size){auto e=m_scene.CreateEntity(name);e.SetParent(panel,false);auto& r=e.AddComponent<RectTransform>();r.Position=position;r.Size=size;e.AddComponent<GuiText>().FontSize=14;return e;};
    m_title=text("Console title",{8,6},{-16,22});m_title.GetComponent<RectTransform>().AnchorMax={1,0};
    m_output=text("Console output",{8,32},{-16,-80});m_output.GetComponent<RectTransform>().AnchorMax={1,1};
    m_input=m_scene.CreateEntity("Command input");m_input.SetParent(panel,false);auto& r=m_input.AddComponent<RectTransform>();r.AnchorMin=r.AnchorMax={0,1};r.AnchorMax.X=1;r.Position={8,-40};r.Size={-16,32};r.Padding={6,2,6,2};m_input.AddComponent<Rectangle>().Style.Color={.08f,.09f,.12f,1};auto& input=m_input.AddComponent<GuiTextInput>();input.MaxLength=4096;input.FontSize=14;input.Placeholder="Enter command (Tab completes, Up/Down history)";
}
void DeveloperConsoleOverlay::Refresh(Vec2 size){
    m_frame.GetComponent<Frame>().Visible=DeveloperConsole::IsVisible();if(!DeveloperConsole::IsVisible())return;
    auto& title=m_title.GetComponent<GuiText>();title.Value="Developer console  |  ` / Escape to close";
    if(CVar::Exists("debug.stats")&&std::get<bool>(CVar::GetValue("debug.stats")))title.Value+="  |  FPS "+std::to_string(int(Application::GetFrameRate()))+"  frame "+std::to_string(Application::GetFrameCount());
    auto messages=DeveloperConsole::GetMessages();const auto lines=std::size_t(std::clamp(int((size.Y*.5f-100)/18),1,40));m_scroll=std::clamp(m_scroll,0,std::max(0,int(messages.size())-int(lines)));auto end=messages.size()-std::size_t(m_scroll),start=end>lines?end-lines:0;std::string value;
    for(auto i=start;i<end;++i){if(!value.empty())value+='\n';if(messages[i].Level==ConsoleLevel::Error)value+="[error] ";else if(messages[i].Level==ConsoleLevel::Warning)value+="[warning] ";value+=messages[i].Text;}m_output.GetComponent<GuiText>().Value=std::move(value);
    m_scene.GetSystem<GuiSystem>().Layout(m_scene,size);
}
bool DeveloperConsoleOverlay::ProcessInput(Vec2 size){
    const bool wasVisible=m_visible;bool toggled=false;
    if(DeveloperConsole::IsEnabled()&&Input::GetKeyDown(KeyCode::Grave)){toggled=true;DeveloperConsole::SetVisible(!DeveloperConsole::IsVisible());InputAccess::GetState()->TextInput.clear();}
    if(DeveloperConsole::IsVisible()&&Input::GetKeyDown(KeyCode::Escape))DeveloperConsole::SetVisible(false);
    const bool visible=DeveloperConsole::IsVisible();Refresh(size);if(!visible){if(m_visible)GUI::ClearFocus(m_scene);m_visible=false;return wasVisible||toggled;}
    if(!m_visible){GUI::Focus(m_input);m_history=-1;m_draft.clear();}m_visible=true;
    auto& input=m_input.GetComponent<GuiTextInput>();auto history=DeveloperConsole::GetHistory();if(m_history>=int(history.size()))m_history=-1;
    if(Input::GetKeyDown(KeyCode::Up)&&!history.empty()){if(m_history<0){m_draft=input.Value;m_history=int(history.size())-1;}else m_history=std::max(0,m_history-1);input.Value=history[m_history];}
    if(Input::GetKeyDown(KeyCode::Down)&&m_history>=0){if(++m_history>=int(history.size())){m_history=-1;input.Value=m_draft;}else input.Value=history[m_history];}
    if(Input::GetKeyDown(KeyCode::Tab)){try{auto matches=DeveloperConsole::Complete(input.Value);if(matches.size()==1)input.Value=matches.front()+" ";else if(!matches.empty()){std::string text;for(auto& name:matches)text+=name+"  ";DeveloperConsole::Log(text,ConsoleLevel::Info,"Completion");}}catch(const std::exception&){} }
    if(Input::GetKeyDown(KeyCode::PageUp))m_scroll+=5;if(Input::GetKeyDown(KeyCode::PageDown))m_scroll-=5;m_scroll+=int(std::round(Input::GetScrollDelta().Y*3));
    m_scene.GetSystem<GuiSystem>().ProcessInput(m_scene);
    if(GUI::GetInteraction(m_input).Submitted){auto line=std::move(input.Value);input.Value.clear();m_history=-1;m_scroll=0;DeveloperConsole::Execute(line);GUI::Focus(m_input);}Refresh(size);return true;
}
void DeveloperConsoleOverlay::Prepare(Vec2 size){Refresh(size);if(DeveloperConsole::IsVisible())m_renderer.PrepareOverlay(m_scene,size);}
void DeveloperConsoleOverlay::Render(Vec2 size){if(DeveloperConsole::IsVisible())m_renderer.RenderOverlay(m_scene,size);}
}
