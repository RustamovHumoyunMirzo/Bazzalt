#include "Runtime/ApplicationAccess.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Bazzalt/Subsystem.h"
#include "Bazzalt/Detail/SubsystemAccess.h"
#include "Bazzalt/Scene.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Bazzalt {
namespace {
struct State {
    int Target=60,Background=30,ExitCode=0;
    bool RunInBackground=true,Focused=true,Playing=false,Editor=false,Quitting=false,ServicesReady=false,SceneReady=false,AudioReady=false,InputReady=false,PhysicsReady=false;
    double SmoothedDelta=0;
    std::uint64_t Frames=0;
    std::uint32_t Width=0,Height=0;
    std::string Name,Company,Version;
};
State& Data(){static State state;return state;}
void CheckRate(int rate){if(rate<0||rate>1000)throw std::out_of_range("Frame rate must be 0 (uncapped) or 1..1000");}
}
Application::Subscription Application::Subscribe(ApplicationEventType type,ApplicationEvents::Callback callback){return ApplicationEvents::Subscribe(type,std::move(callback));}
bool Application::Unsubscribe(Subscription id){return ApplicationEvents::Unsubscribe(id);}
void Application::SetTargetFrameRate(int rate){CheckRate(rate);Data().Target=rate;}
int Application::GetTargetFrameRate(){return Data().Target;}
void Application::SetBackgroundFrameRate(int rate){CheckRate(rate);Data().Background=rate;}
int Application::GetBackgroundFrameRate(){return Data().Background;}
void Application::SetRunInBackground(bool value){Data().RunInBackground=value;}
bool Application::GetRunInBackground(){return Data().RunInBackground;}
double Application::GetFrameRate(){return Data().SmoothedDelta>0?1.0/Data().SmoothedDelta:0;}
std::uint64_t Application::GetFrameCount(){return Data().Frames;}
bool Application::IsFocused(){return Data().Focused;}
bool Application::IsPlaying(){return Data().Playing;}
bool Application::IsEditor(){return Data().Editor;}
bool Application::IsQuitting(){return Data().Quitting;}
void Application::Quit(int code){if(code<0||code>255)throw std::out_of_range("Exit code must be 0..255");Data().ExitCode=code;Data().Quitting=true;}
int Application::GetExitCode(){return Data().ExitCode;}
std::uint32_t Application::GetWindowWidth(){return Data().Width;}
std::uint32_t Application::GetWindowHeight(){return Data().Height;}
std::string Application::GetName(){return Data().Name;}
std::string Application::GetCompanyName(){return Data().Company;}
std::string Application::GetVersion(){return Data().Version;}
std::string Application::GetPlatform(){
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}
Scene& Subsystem::GetScene(){auto* scene=SceneManager::GetActiveScene();if(!scene)throw std::logic_error("No active scene");return *scene;}
bool Runtime::ApplicationAccess::AreRuntimeServicesReady(){return Data().InputReady&&Data().AudioReady&&Data().PhysicsReady;}
void Runtime::ApplicationAccess::Reset(bool editor,const ProjectMetadata& project){
    Data()={};Data().Editor=editor;Data().Name=project.Name;
    if(auto found=project.Properties.find("project.company");found!=project.Properties.end())Data().Company=found->second;
    if(auto found=project.Properties.find("project.version");found!=project.Properties.end())Data().Version=found->second;
}
void Runtime::ApplicationAccess::SetInputReady(bool ready){Data().InputReady=ready;}
void Runtime::ApplicationAccess::SetPhysicsReady(bool ready){Data().PhysicsReady=ready;}
void Runtime::ApplicationAccess::SetServicesReady(bool ready){Data().ServicesReady=ready;}
void Runtime::ApplicationAccess::SetSceneReady(bool ready){Data().SceneReady=ready;}
void Runtime::ApplicationAccess::SetAudioReady(bool ready){Data().AudioReady=ready;}
void Runtime::ApplicationAccess::Observe(const ApplicationEvent& event){
    switch(event.Type){
    case ApplicationEventType::OnWindowFocusChanged:Data().Focused=event.IsFocused;break;
    case ApplicationEventType::OnWindowResize:Data().Width=event.Width;Data().Height=event.Height;break;
    case ApplicationEventType::OnPlay:Data().Playing=true;break;
    case ApplicationEventType::OnStop:Data().Playing=false;break;
    default:break;
    }
}
void Runtime::ApplicationAccess::Advance(double delta){if(!std::isfinite(delta)||delta<=0)return;auto& state=Data();state.SmoothedDelta=state.Frames?state.SmoothedDelta+(delta-state.SmoothedDelta)*.1:delta;++state.Frames;}
int Runtime::ApplicationAccess::GetEffectiveFrameRate(){const auto& state=Data();if(state.Focused)return state.Target;if(!state.Target)return state.Background;if(!state.Background)return state.Target;return std::min(state.Target,state.Background);}
void Detail::RequireSubsystemService(SubsystemService service,const char* operation){
    if(Runtime::ApplicationEventsAccess::GetSubsystemLevel()<0)return;
    const bool ready=service==SubsystemService::Scene?Data().SceneReady:service==SubsystemService::Audio?Data().AudioReady:service==SubsystemService::Input?Data().InputReady:service==SubsystemService::Physics?Data().PhysicsReady:Data().ServicesReady;
    if(!ready)throw std::logic_error(std::string(operation)+": service is not initialized; subscribe to a later application event");
}
}
