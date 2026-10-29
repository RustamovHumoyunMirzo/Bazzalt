#pragma once
#include "Bazzalt/ApplicationEvents.h"
namespace Bazzalt::Runtime {
struct ApplicationEventsAccess {
    static void Emit(ApplicationEvent event) noexcept;
    static ApplicationEvents::Subscription Subscribe(ApplicationEventType type, ApplicationEvents::Callback callback, const void* owner);
    static void DisconnectOwner(const void* owner) noexcept;
    static const void* SetOwner(const void* owner) noexcept;
    static int SetSubsystemLevel(int level) noexcept;
    static int GetSubsystemLevel() noexcept;
    static bool SetAssetAccess(bool enabled) noexcept;
    static bool GetAssetAccess() noexcept;
    static int GetLevelOrder(int level) noexcept;
    static int GetEventLevel(ApplicationEventType type) noexcept;
    static void BindStageLoader(std::function<void(int)> loader);
    static void CompleteInitialization(const std::string& sceneUUID,bool editor);
    static void ThrowIfFailed();
    static void Fail(const std::string& error) noexcept;
};
}
