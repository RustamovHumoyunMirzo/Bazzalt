#pragma once
#include "Bazzalt/AI.h"
#include <vector>
namespace Bazzalt::AI {
// Private module-owned execution state. Only opaque handles cross the module table.
class BehaviorTreeRuntime final {
public:
    static BehaviorTreeRuntime FromSource(const std::string& xml);
    static BehaviorTreeRuntime Load(UUID asset);
    static std::vector<UUID> References(const std::string& source);
    BehaviorTreeRuntime();
    ~BehaviorTreeRuntime();
    BehaviorTreeRuntime(BehaviorTreeRuntime&&) noexcept;
    BehaviorTreeRuntime& operator=(BehaviorTreeRuntime&&) noexcept;
    Status Tick(Context& context);
    void Reset(Context& context);
    void Initialize(Blackboard& memory) const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
