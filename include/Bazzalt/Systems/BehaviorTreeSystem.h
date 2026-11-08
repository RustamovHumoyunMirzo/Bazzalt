#pragma once
#include "Bazzalt/AI.h"
#include "Bazzalt/System.h"
#include <utility>
namespace Bazzalt {
class Scene;
// Assignment-compatible callback slot. Copies are callable snapshots; system fields track
// ownership.
template <class Function> class BehaviorHandlerSlot {
  public:
    BehaviorHandlerSlot(const BehaviorHandlerSlot &other) : m_callback(other.m_callback) {}
    BehaviorHandlerSlot &operator=(const BehaviorHandlerSlot &other) {
        return *this = other.m_callback;
    }
    BehaviorHandlerSlot &operator=(Function callback) {
        if (m_assign)
            m_assign(std::move(callback));
        else
            m_callback = std::move(callback);
        return *this;
    }
    explicit operator bool() const {
        return bool(m_callback);
    }
    operator Function() const {
        return m_callback;
    }
    Function Get() const {
        return m_callback;
    }
    template <class... Args> decltype(auto) operator()(Args &&...args) const {
        return m_callback(std::forward<Args>(args)...);
    }

  private:
    friend class BehaviorTreeSystem;
    explicit BehaviorHandlerSlot(std::function<void(Function)> assign)
        : m_assign(std::move(assign)) {}
    void Store(Function callback) {
        m_callback = std::move(callback);
    }
    Function m_callback;
    std::function<void(Function)> m_assign;
};
// Default scene system; ticks agents at the fixed gameplay rate before Jolt simulation.
class BAZZALT_API BehaviorTreeSystem final : public System {
  public:
    BehaviorTreeSystem();
    ~BehaviorTreeSystem() override;
    AI::Blackboard &GetBlackboard(Entity entity);
    void SetHandlers(Entity entity, AI::ActionHandler action, AI::ConditionHandler condition,
                     const void *owner = nullptr);
    void ClearHandlers(const void *owner);
    void Reset(Entity entity);
    BehaviorHandlerSlot<AI::ActionHandler> Action;
    BehaviorHandlerSlot<AI::ConditionHandler> Condition;

  private:
    friend class Scene;
    void ClearScene(Scene &);
    void SetDefaultAction(AI::ActionHandler);
    void SetDefaultCondition(AI::ConditionHandler);
    void OnUpdate(Scene &, float) override {}
    void OnFixedUpdate(Scene &, float) override;
    void OnDestroy(Scene &) override;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace Bazzalt
