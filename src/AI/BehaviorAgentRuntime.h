#pragma once
#include "Bazzalt/Systems/BehaviorTreeSystem.h"
namespace Bazzalt {
class BehaviorAgentRuntime final {
public:
    explicit BehaviorAgentRuntime(BehaviorTreeSystem& owner);
    ~BehaviorAgentRuntime();
    AI::Blackboard& GetBlackboard(Entity entity);
    void SetHandlers(Entity entity,AI::ActionHandler action,AI::ConditionHandler condition,const void* owner);
    void ClearHandlers(const void* owner);
    void Reset(Entity entity);
    void ResetDefaults(bool action,bool condition);
    void Tick(Scene& scene,float dt);
    void Release(Scene& scene);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    BehaviorTreeSystem& Owner;
};
}
