#include "Bazzalt/Systems/BehaviorTreeSystem.h"
#include "AI/BehaviorLifecycle.h"
#include "AI/BehaviorModuleApi.h"
#include "Bazzalt/Components/BehaviorTreeAgent.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Scene.h"
#include "Runtime/ApplicationEventsAccess.h"
#include "Runtime/ExtensionAccess.h"
#include "Runtime/GlobalEventsAccess.h"
#include "Runtime/GraphicsAccess.h"
#include <set>
namespace Bazzalt {
namespace {
std::set<BehaviorTreeSystem *> &Systems() {
    static std::set<BehaviorTreeSystem *> systems;
    return systems;
}
struct HandlerState {
    // Destroy gameplay std::functions before releasing their native code.
    std::shared_ptr<void> Module;
    AI::ActionHandler Action;
    AI::ConditionHandler Condition;
    const void *Owner = nullptr;
    const void *ScriptOwner = nullptr;
    std::string Extension;
    Entity Object;
    bool Disabled = false;
};
struct CallbackScope {
    std::shared_ptr<void> PreviousModule;
    const void *PreviousOwner = nullptr;
    const void *PreviousGraphics = nullptr;
    std::string PreviousExtension;
    bool Native = false;
    explicit CallbackScope(const HandlerState &state) : Native(bool(state.Module)) {
        if (!Native)
            return;
        PreviousModule = Detail::SetCurrentGameplayModule(state.Module);
        PreviousOwner = Runtime::ApplicationEventsAccess::SetOwner(state.ScriptOwner);
        PreviousGraphics = Runtime::GraphicsAccess::SetScope(state.ScriptOwner);
        PreviousExtension = Runtime::ExtensionAccess::SetCaller(state.Extension);
    }
    ~CallbackScope() {
        if (Native) {
            Runtime::ExtensionAccess::SetCaller(std::move(PreviousExtension));
            Runtime::GraphicsAccess::SetScope(PreviousGraphics);
            Runtime::ApplicationEventsAccess::SetOwner(PreviousOwner);
            Detail::SetCurrentGameplayModule(std::move(PreviousModule));
        }
    }
};
std::shared_ptr<HandlerState> Capture(AI::ActionHandler action, AI::ConditionHandler condition,
                                      const void *owner) {
    auto state = std::make_shared<HandlerState>();
    state->Module = Detail::GetCurrentGameplayModule();
    state->ScriptOwner = state->Module ? Runtime::ApplicationEventsAccess::GetOwner() : nullptr;
    state->Owner = owner ? owner : state->ScriptOwner;
    if ((action || condition) && (Runtime::GlobalEventsAccess::IsOwnerClosing(state->ScriptOwner) ||
                                  Runtime::GlobalEventsAccess::IsOwnerClosing(state->Owner)))
        throw std::logic_error("Cannot register AI callbacks while their owner is closing");
    state->Action = std::move(action);
    state->Condition = std::move(condition);
    state->Extension = Runtime::ExtensionAccess::SetCaller("");
    Runtime::ExtensionAccess::SetCaller(state->Extension);
    return state;
}
AI::ActionHandler WrapAction(const std::shared_ptr<HandlerState> &state) {
    if (!state->Action)
        return {};
    return [state](const std::string &name, AI::Context &context, bool cancel) {
        if (state->Disabled)
            return AI::Status::Failure;
        CallbackScope scope(*state);
        return state->Action(name, context, cancel);
    };
}
AI::ConditionHandler WrapCondition(const std::shared_ptr<HandlerState> &state) {
    if (!state->Condition)
        return {};
    return [state](const std::string &name, AI::Context &context) {
        if (state->Disabled)
            return false;
        CallbackScope scope(*state);
        return state->Condition(name, context);
    };
}
bool Owned(const std::shared_ptr<HandlerState> &state, const void *owner) {
    return state && (state->Owner == owner || state->ScriptOwner == owner);
}
} // namespace
struct BehaviorTreeSystem::Impl {
    const Runtime::BehaviorModuleApi *Api = nullptr;
    void *Handle = nullptr;
    std::map<UUID, std::shared_ptr<HandlerState>> Handlers;
    std::shared_ptr<HandlerState> DefaultAction, DefaultCondition;
    void Ensure(BehaviorTreeSystem &owner) {
        if (!Handle) {
            Api = &Runtime::BehaviorModule();
            Handle = Api->CreateAgents(owner);
        }
    }
    ~Impl() {
        if (Handle)
            Api->DestroyAgents(Handle);
    }
};
BehaviorTreeSystem::BehaviorTreeSystem()
    : Action([this](AI::ActionHandler callback) { SetDefaultAction(std::move(callback)); }),
      Condition(
          [this](AI::ConditionHandler callback) { SetDefaultCondition(std::move(callback)); }),
      m_impl(std::make_unique<Impl>()) {
    Systems().insert(this);
}
BehaviorTreeSystem::~BehaviorTreeSystem() {
    for (auto& [id, state] : m_impl->Handlers) state->Disabled = true;
    if (m_impl->DefaultAction) m_impl->DefaultAction->Disabled = true;
    if (m_impl->DefaultCondition) m_impl->DefaultCondition->Disabled = true;
    Systems().erase(this);
}
AI::Blackboard &BehaviorTreeSystem::GetBlackboard(Entity entity) {
    m_impl->Ensure(*this);
    return m_impl->Api->GetBlackboard(m_impl->Handle, entity);
}
void BehaviorTreeSystem::SetHandlers(Entity entity, AI::ActionHandler action,
                                     AI::ConditionHandler condition, const void *owner) {
    auto state = Capture(std::move(action), std::move(condition), owner);
    state->Object = entity;
    m_impl->Ensure(*this);
    m_impl->Api->SetHandlers(m_impl->Handle, entity, WrapAction(state), WrapCondition(state),
                             state.get());
    auto old = m_impl->Handlers.find(entity.GetUUID());
    if (old != m_impl->Handlers.end()) old->second->Disabled = true;
    m_impl->Handlers[entity.GetUUID()] = std::move(state);
}
void BehaviorTreeSystem::ClearHandlers(const void *owner) {
    // Snapshot registrations so cancellation may safely change other handlers.
    std::vector<std::pair<UUID, std::shared_ptr<HandlerState>>> pending;
    for (const auto &row : m_impl->Handlers)
        if (Owned(row.second, owner))
            pending.push_back(row);
    for (auto &[id, state] : pending) {
        try {
            m_impl->Api->ClearHandlers(m_impl->Handle, state.get());
        } catch (const std::exception &error) {
            Console::Error(error.what(), "AI");
        }
        state->Disabled = true;
        auto current = m_impl->Handlers.find(id);
        if (current != m_impl->Handlers.end() && current->second == state)
            m_impl->Handlers.erase(current);
    }
    if (Owned(m_impl->DefaultAction, owner))
        SetDefaultAction({});
    if (Owned(m_impl->DefaultCondition, owner))
        SetDefaultCondition({});
}
void BehaviorTreeSystem::SetDefaultAction(AI::ActionHandler callback) {
    auto state =
        callback ? Capture(std::move(callback), {}, nullptr) : std::shared_ptr<HandlerState>{};
    if (m_impl->Handle)
        m_impl->Api->ResetDefaults(m_impl->Handle, true, false);
    if (m_impl->DefaultAction)
        m_impl->DefaultAction->Disabled = true;
    Action.Store(state ? WrapAction(state) : AI::ActionHandler{});
    m_impl->DefaultAction = std::move(state);
}
void BehaviorTreeSystem::SetDefaultCondition(AI::ConditionHandler callback) {
    auto state =
        callback ? Capture({}, std::move(callback), nullptr) : std::shared_ptr<HandlerState>{};
    if (m_impl->Handle)
        m_impl->Api->ResetDefaults(m_impl->Handle, false, true);
    if (m_impl->DefaultCondition)
        m_impl->DefaultCondition->Disabled = true;
    Condition.Store(state ? WrapCondition(state) : AI::ConditionHandler{});
    m_impl->DefaultCondition = std::move(state);
}
void BehaviorTreeSystem::Reset(Entity entity) {
    m_impl->Ensure(*this);
    m_impl->Api->ResetAgent(m_impl->Handle, entity);
}
void BehaviorTreeSystem::OnFixedUpdate(Scene &scene, float dt) {
    if (!m_impl->Handle && scene.GetRegistry().view<BehaviorTreeAgent>().empty())
        return;
    m_impl->Ensure(*this);
    m_impl->Api->StepAgents(m_impl->Handle, scene, dt);
    std::erase_if(m_impl->Handlers, [](auto &row) {
        const bool removed = !row.second->Object ||
               !row.second->Object.template HasComponent<BehaviorTreeAgent>();
        if (removed) row.second->Disabled = true;
        return removed;
    });
}
void BehaviorTreeSystem::ClearScene(Scene &scene) {
    if (m_impl->Handle)
        m_impl->Api->ReleaseAgents(m_impl->Handle, scene);
    for (auto &[id, state] : m_impl->Handlers)
        state->Disabled = true;
    m_impl->Handlers.clear();
    if (m_impl->DefaultAction)
        m_impl->DefaultAction->Disabled = true;
    if (m_impl->DefaultCondition)
        m_impl->DefaultCondition->Disabled = true;
    Action.Store({});
    Condition.Store({});
    m_impl->DefaultAction.reset();
    m_impl->DefaultCondition.reset();
}
void BehaviorTreeSystem::OnDestroy(Scene &scene) {
    ClearScene(scene);
}
} // namespace Bazzalt
namespace Bazzalt::Runtime {
void DisconnectAIHandlers(const void *owner) noexcept {
    if (!owner)
        return;
    auto previous = GlobalEventsAccess::SetClosingOwner(owner);
    auto systems = Systems();
    for (auto *system : systems)
        if (Systems().contains(system))
            try {
                system->ClearHandlers(owner);
            } catch (const std::exception &error) {
                Console::Error(error.what(), "AI");
            } catch (...) {
            }
    GlobalEventsAccess::SetClosingOwner(previous);
}
} // namespace Bazzalt::Runtime
