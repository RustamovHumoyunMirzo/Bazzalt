#pragma once
#include "Bazzalt/Export.h"
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <utility>
namespace Bazzalt {
/// Shared-future handle. Waiting rethrows the job's exception. Safe to copy.
class BAZZALT_API JobHandle {
public:
    JobHandle() = default;
    bool IsComplete() const;
    void Wait() const;
private:
    friend class Jobs;
    explicit JobHandle(std::shared_future<void> future) : m_future(std::move(future)) {}
    std::shared_future<void> m_future;
};
/// CPU-only work. Never access scenes, Lua, Qt or graphics from worker callbacks.
class BAZZALT_API Jobs final {
public:
    Jobs() = delete;
    static JobHandle Schedule(std::function<void()> work);
    /// Runs [begin,end) batches; callback receives an exclusive end index.
    static JobHandle ParallelFor(std::size_t count, std::size_t batchSize,
        std::function<void(std::size_t, std::size_t)> work);
    static unsigned GetWorkerCount();
    /// Host-thread barrier. Stop producers first; never call from a worker.
    /// Exceptions belong to individual handles and are not rethrown here.
    static void WaitAll();
};
}
