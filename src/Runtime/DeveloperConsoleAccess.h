#pragma once
namespace Bazzalt::Runtime {
struct DeveloperConsoleAccess {
    static void BindThread();
    static void Initialize(bool editor);
    static void DisconnectOwner(const void* owner) noexcept;
    static void Reset();
};
}
