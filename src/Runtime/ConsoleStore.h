#pragma once
#include "Bazzalt/Console.h"
namespace Bazzalt::Runtime {
struct ConsoleSnapshot {std::uint64_t Revision;std::vector<ConsoleMessage> Messages;};
class ConsoleAccess final {
public:
    static void Acquire();
    static void Release() noexcept;
    static std::optional<ConsoleSnapshot> Snapshot(std::uint64_t afterRevision);
};
}
