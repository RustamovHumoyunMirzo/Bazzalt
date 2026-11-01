#include "Runtime/JobAccess.h"
#include <unordered_map>
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Profiler.h"
#include "Bazzalt/Detail/ModuleLifetime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace Bazzalt {
namespace {
thread_local bool Worker = false;
struct OwnedWork {
    // Keep code loaded until the callback and all its captures are destroyed.
    std::shared_ptr<void> Module;
    std::function<void()> Function;
    void operator()() {
        struct Restore {
            std::shared_ptr<void> Previous;
            ~Restore(){Detail::SetCurrentGameplayModule(std::move(Previous));}
        } restore{Detail::SetCurrentGameplayModule(Module)};
        ProfileScope scope("Jobs.Execute");
        Function();
    }
};
class Pool;
std::atomic<Pool*> Existing{nullptr};
class Pool {
public:
    std::mutex Mutex;
    std::condition_variable Ready;
    std::deque<std::function<void()>> Queue;
    std::vector<std::thread> Threads;
    bool Stopping = false;
    std::size_t Pending = 0;
    std::unordered_map<const void*,std::size_t> ModulePending;
    std::condition_variable Idle;
    Pool() {
        (void)Profiler::IsCapturing(); // Capture storage outlives worker shutdown.
        const auto hardware = std::thread::hardware_concurrency();
        const auto count = std::clamp(hardware > 1 ? hardware - 1 : 1u, 1u, 8u);
        try { for(unsigned i=0;i<count;++i) Threads.emplace_back([this] {
            Worker = true;
            for(;;) {
                std::function<void()> work;
                { std::unique_lock lock(Mutex); Ready.wait(lock,[this]{return Stopping || !Queue.empty();});
                  if(Queue.empty()) return;
                  work=std::move(Queue.front()); Queue.pop_front(); }
                work();
            }
        }); } catch(...) { Stop(); throw; }
    }
    void Stop() {
        {std::lock_guard lock(Mutex); Stopping=true;}
        Ready.notify_all();
        for(auto& thread:Threads) if(thread.joinable()) thread.join();
    }
    ~Pool() { Stop(); Existing=nullptr; }
    bool Help() {
        std::function<void()> work;
        {std::lock_guard lock(Mutex); if(Queue.empty()) return false;
         work=std::move(Queue.front()); Queue.pop_front();}
        work(); return true;
    }
};
Pool& GetPool() { static Pool pool; Existing=&pool; return pool; }
}
bool JobHandle::IsComplete() const {
    return !m_future.valid() || m_future.wait_for(std::chrono::seconds(0))==std::future_status::ready;
}
void JobHandle::Wait() const {
    if(!m_future.valid()) return;
    // Nested fork/join must not starve when every worker waits on a child.
    while(Worker && !IsComplete()) if(!GetPool().Help()) m_future.wait_for(std::chrono::milliseconds(1));
    m_future.get();
}
JobHandle Jobs::Schedule(std::function<void()> work) {
    if(!work) throw std::invalid_argument("Job callback is empty");
    auto module=Detail::GetCurrentGameplayModule();
    auto task=std::make_shared<std::packaged_task<void()>>(OwnedWork{module,std::move(work)});
    JobHandle handle(task->get_future().share());
    auto& pool=GetPool();
    {std::lock_guard lock(pool.Mutex);
     if(pool.Stopping) throw std::runtime_error("Job scheduler is shutting down");
     if(module)pool.ModulePending.try_emplace(module.get(),0);
     try{pool.Queue.emplace_back([task,&pool,module]() mutable {
         (*task)();
         // Destroy the queued closure before signaling the teardown barrier.
         task.reset();
         {std::lock_guard lock(pool.Mutex);--pool.Pending;if(module){auto found=pool.ModulePending.find(module.get());if(--found->second==0)pool.ModulePending.erase(found);}}
         pool.Idle.notify_all();
     });}catch(...){if(module&&pool.ModulePending.at(module.get())==0)pool.ModulePending.erase(module.get());throw;}
     ++pool.Pending;if(module)++pool.ModulePending.at(module.get());}
    pool.Ready.notify_one(); return handle;
}
JobHandle Jobs::ParallelFor(std::size_t count,std::size_t batchSize,std::function<void(std::size_t,std::size_t)> work) {
    if(!batchSize || !work) throw std::invalid_argument("Invalid parallel loop");
    // Bound scheduling overhead and queue growth to worker count.
    return Schedule([count,batchSize,work=std::move(work)] {
        auto next=std::make_shared<std::atomic<std::size_t>>(0);
        const auto batches=count/batchSize+(count%batchSize!=0);
        std::vector<JobHandle> handles;
        const auto childCount=std::min<std::size_t>(batches,GetWorkerCount());
        handles.reserve(childCount);
        std::exception_ptr failure;
        try {for(std::size_t i=0;i<childCount;++i)
            handles.push_back(Schedule([=] {
                for(;;) {
                    auto batch=next->load();
                    while(batch<batches && !next->compare_exchange_weak(batch,batch+1)) {}
                    if(batch>=batches) break;
                    auto begin=batch*batchSize;
                    work(begin,begin+std::min(batchSize,count-begin));
                }
            }));
        }catch(...) {failure=std::current_exception();}
        for(auto& handle:handles) try {handle.Wait();}catch(...) {if(!failure) failure=std::current_exception();}
        if(failure) std::rethrow_exception(failure);
    });
}
void Jobs::WaitAll() {
    if(Worker) throw std::logic_error("WaitAll cannot run on a job worker");
    auto* pool=Existing.load();
    if(!pool) return;
    std::unique_lock lock(pool->Mutex);
    pool->Idle.wait(lock,[pool]{return pool->Pending==0;});
}
void Runtime::JobAccess::WaitForModule(const std::shared_ptr<void>& module) {
    if(!module)return;
    if(Worker)throw std::logic_error("Module teardown cannot run on a job worker");
    auto* pool=Existing.load();if(!pool)return;
    std::unique_lock lock(pool->Mutex);pool->Idle.wait(lock,[&]{return !pool->ModulePending.contains(module.get());});
}
unsigned Jobs::GetWorkerCount() {return static_cast<unsigned>(GetPool().Threads.size());}
}
