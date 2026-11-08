#pragma once
namespace Bazzalt::Runtime {
// Called while script objects and their module leases are still alive.
void DisconnectAIHandlers(const void *owner) noexcept;
} // namespace Bazzalt::Runtime
