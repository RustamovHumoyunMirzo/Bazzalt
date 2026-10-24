#pragma once
#include "Bazzalt/ApplicationEvents.h"
namespace Bazzalt::Runtime {
struct ApplicationEventsAccess {
    static void Emit(ApplicationEvent event) noexcept;
    static ApplicationEvents::Subscription Subscribe(ApplicationEventType type, ApplicationEvents::Callback callback, const void* owner);
    static void DisconnectOwner(const void* owner) noexcept;
    static const void* SetOwner(const void* owner) noexcept;
};
}
