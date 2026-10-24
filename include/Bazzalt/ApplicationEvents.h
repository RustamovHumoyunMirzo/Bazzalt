#pragma once
#include "Bazzalt/Export.h"
#include <cstdint>
#include <functional>
#include <string>

namespace Bazzalt {
enum class ApplicationEventType {
    OnBoot, OnBeforeWindowCreate, OnInit, OnLoad, OnPostWindowCreate,
    OnPreUpdate, OnUpdate, OnFixedUpdate, OnLateUpdate, OnPreRender, OnRender, OnPostRender,
    OnWindowResize, OnWindowFocusChanged, OnWindowCloseRequested,
    OnSceneLoad, OnSceneLoaded, OnSceneUnload, OnSceneUnloaded,
    OnPlay, OnStop, OnPreShutdown, OnShutdown
};
struct ApplicationEvent {
    ApplicationEventType Type{};
    float DeltaTime=0;
    std::uint32_t Width=0, Height=0;
    bool IsFocused=false;
    std::string SceneUUID;
    std::string ScenePath;
};
// Main-thread notifications, not ownership of the engine loop. No event replay.
class BAZZALT_API ApplicationEvents final {
public:
    using Subscription=std::uint64_t;
    using Callback=std::function<void(const ApplicationEvent&)>;
    static Subscription Subscribe(ApplicationEventType type, Callback callback);
    static bool Unsubscribe(Subscription subscription);
    static const char* GetName(ApplicationEventType type);
};
}
