#pragma once
#include "Bazzalt/ApplicationEvents.h"
#include <utility>
namespace Bazzalt {
class BAZZALT_API Application final {
public:
    Application()=delete;
    using Subscription=ApplicationEvents::Subscription;
    // Only C++ subsystem scopes may register global event listeners.
    static Subscription Subscribe(ApplicationEventType type,ApplicationEvents::Callback callback);
    static bool Unsubscribe(Subscription subscription);
    static void SetTargetFrameRate(int framesPerSecond); // 0 = uncapped; otherwise 1..1000.
    static int GetTargetFrameRate();
    static void SetBackgroundFrameRate(int framesPerSecond);
    static int GetBackgroundFrameRate();
    static void SetRunInBackground(bool enabled);
    static bool GetRunInBackground();
    static double GetFrameRate();
    static std::uint64_t GetFrameCount();
    static bool IsFocused();
    static bool IsPlaying();
    static bool IsEditor();
    static bool IsQuitting();
    static void Quit(int exitCode=0);
    static int GetExitCode();
    static std::uint32_t GetWindowWidth();
    static std::uint32_t GetWindowHeight();
    static std::string GetName();
    static std::string GetCompanyName();
    static std::string GetVersion();
    static std::string GetPlatform();
#define BAZZALT_APP_SIGNAL(Name) \
    static Subscription Name(std::function<void()> callback) { return Subscribe(ApplicationEventType::Name,[callback=std::move(callback)](const ApplicationEvent&){callback();}); }
    BAZZALT_APP_SIGNAL(OnBoot)
    BAZZALT_APP_SIGNAL(OnBeforeWindowCreate)
    BAZZALT_APP_SIGNAL(OnInit)
    BAZZALT_APP_SIGNAL(OnLoad)
    BAZZALT_APP_SIGNAL(OnPostWindowCreate)
    BAZZALT_APP_SIGNAL(OnPreRender)
    BAZZALT_APP_SIGNAL(OnRender)
    BAZZALT_APP_SIGNAL(OnPostRender)
    BAZZALT_APP_SIGNAL(OnWindowCloseRequested)
    BAZZALT_APP_SIGNAL(OnPlay)
    BAZZALT_APP_SIGNAL(OnStop)
    BAZZALT_APP_SIGNAL(OnPreShutdown)
    BAZZALT_APP_SIGNAL(OnShutdown)
    BAZZALT_APP_SIGNAL(OnBeforeInputInit)
    BAZZALT_APP_SIGNAL(OnInputInit)
    BAZZALT_APP_SIGNAL(OnAfterInputInit)
    BAZZALT_APP_SIGNAL(OnBeforeAudioInit)
    BAZZALT_APP_SIGNAL(OnAudioInit)
    BAZZALT_APP_SIGNAL(OnAfterAudioInit)
    BAZZALT_APP_SIGNAL(OnBeforePhysicsInit)
    BAZZALT_APP_SIGNAL(OnPhysicsInit)
    BAZZALT_APP_SIGNAL(OnAfterPhysicsInit)
    BAZZALT_APP_SIGNAL(OnBeforePhysicsShutdown)
    BAZZALT_APP_SIGNAL(OnAfterPhysicsShutdown)
#undef BAZZALT_APP_SIGNAL
#define BAZZALT_APP_TIMED_SIGNAL(Name) \
    static Subscription Name(std::function<void(float)> callback) { return Subscribe(ApplicationEventType::Name,[callback=std::move(callback)](const ApplicationEvent& e){callback(e.DeltaTime);}); }
    BAZZALT_APP_TIMED_SIGNAL(OnPreUpdate)
    BAZZALT_APP_TIMED_SIGNAL(OnUpdate)
    BAZZALT_APP_TIMED_SIGNAL(OnFixedUpdate)
    BAZZALT_APP_TIMED_SIGNAL(OnBeforePhysicsStep)
    BAZZALT_APP_TIMED_SIGNAL(OnAfterPhysicsStep)
    BAZZALT_APP_TIMED_SIGNAL(OnLateUpdate)
#undef BAZZALT_APP_TIMED_SIGNAL
    static Subscription OnWindowResize(std::function<void(std::uint32_t,std::uint32_t)> callback) {return Subscribe(ApplicationEventType::OnWindowResize,[callback=std::move(callback)](const ApplicationEvent& e){callback(e.Width,e.Height);});}
    static Subscription OnWindowFocusChanged(std::function<void(bool)> callback) {return Subscribe(ApplicationEventType::OnWindowFocusChanged,[callback=std::move(callback)](const ApplicationEvent& e){callback(e.IsFocused);});}
#define BAZZALT_APP_SCENE_SIGNAL(Name,Field) \
    static Subscription Name(std::function<void(const std::string&)> callback) { return Subscribe(ApplicationEventType::Name,[callback=std::move(callback)](const ApplicationEvent& e){callback(e.Field);}); }
    BAZZALT_APP_SCENE_SIGNAL(OnSceneLoad,ScenePath)
    BAZZALT_APP_SCENE_SIGNAL(OnSceneLoaded,SceneUUID)
    BAZZALT_APP_SCENE_SIGNAL(OnSceneUnload,SceneUUID)
    BAZZALT_APP_SCENE_SIGNAL(OnSceneUnloaded,SceneUUID)
#undef BAZZALT_APP_SCENE_SIGNAL
};
}
