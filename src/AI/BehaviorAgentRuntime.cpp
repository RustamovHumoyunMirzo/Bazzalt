#include "AI/BehaviorAgentRuntime.h"
#include "AI/BehaviorTreeRuntime.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/BehaviorTreeAgent.h"
#include "Bazzalt/Console.h"
#include "Bazzalt/Scene.h"
namespace Bazzalt {
struct BehaviorAgentRuntime::Impl {
    struct Agent {
        AI::BehaviorTreeRuntime Tree;
        AI::Blackboard Memory;
        UUID Asset{};
        bool Loaded = false, Done = false, Failed = false;
        std::filesystem::path Cache;
        std::string Error;
        AI::ActionHandler Action;
        AI::ConditionHandler Condition;
        const void *Owner = nullptr;
        Entity Object;
    };
    std::map<UUID, std::shared_ptr<Agent>> Agents;
    static AI::Context Context(Agent &agent, BehaviorTreeSystem &system, float dt = 0) {
        return {agent.Object, agent.Memory, dt, agent.Action ? agent.Action : system.Action.Get(),
                agent.Condition ? agent.Condition : system.Condition.Get()};
    }
    static void Cancel(Agent &agent, BehaviorTreeSystem &system) {
        try {
            auto context = Context(agent, system);
            agent.Tree.Reset(context);
        } catch (const std::exception &error) {
            Console::Error(error.what(), "AI");
        }
    }
    std::shared_ptr<Agent> Get(Entity entity, BehaviorTreeSystem &system) {
        auto found = Agents.find(entity.GetUUID());
        if (found != Agents.end() && found->second->Object != entity) {
            auto old = found->second;
            Agents.erase(found);
            Cancel(*old, system);
        }
        auto &entry = Agents[entity.GetUUID()];
        if (!entry) {
            entry = std::make_shared<Agent>();
            entry->Object = entity;
        }
        return entry;
    }
};
BehaviorAgentRuntime::BehaviorAgentRuntime(BehaviorTreeSystem &owner)
    : m_impl(std::make_unique<Impl>()), Owner(owner) {}
BehaviorAgentRuntime::~BehaviorAgentRuntime() = default;
AI::Blackboard &BehaviorAgentRuntime::GetBlackboard(Entity entity) {
    if (!entity || !entity.HasComponent<BehaviorTreeAgent>() ||
        !entity.GetScene()->HasSystem<BehaviorTreeSystem>() ||
        &entity.GetScene()->GetSystem<BehaviorTreeSystem>() != &Owner)
        throw std::invalid_argument("Expected a behavior agent in this scene");
    return m_impl->Get(entity, Owner)->Memory;
}
void BehaviorAgentRuntime::Tick(Scene &scene, float dt) {
    std::vector<std::shared_ptr<Impl::Agent>> removed;
    std::erase_if(m_impl->Agents, [&](const auto &row) {
        auto e = scene.GetEntity(row.first);
        if (e && e == row.second->Object && e.template HasComponent<BehaviorTreeAgent>())
            return false;
        removed.push_back(row.second);
        return true;
    });
    for (auto &agent : removed)
        Impl::Cancel(*agent, Owner);
    std::vector<Entity> entities;
    for (auto handle : scene.GetRegistry().view<BehaviorTreeAgent>())
        entities.push_back(scene.GetEntity(static_cast<Entity::Id>(handle)));
    for (auto e : entities) {
        if (!e || !e.HasComponent<BehaviorTreeAgent>())
            continue;
        auto config = e.GetComponent<BehaviorTreeAgent>();
        auto agent = m_impl->Get(e, Owner);
        auto context = Impl::Context(*agent, Owner, dt);
        if (!config.Enabled || !config.Tree) {
            Impl::Cancel(*agent, Owner);
            continue;
        }
        try {
            auto asset = AssetManager::GetAsset(config.Tree);
            if (!asset || asset->State != AssetState::Ready)
                throw std::invalid_argument("Behavior tree asset is not ready");
            if (!agent->Loaded || agent->Asset != config.Tree || agent->Cache != asset->CachePath) {
                auto tree = AI::BehaviorTreeRuntime::Load(config.Tree);
                agent->Tree.Reset(context);
                agent->Tree = std::move(tree);
                agent->Asset = config.Tree;
                agent->Loaded = true;
                agent->Done = false;
                agent->Failed = false;
                agent->Cache = asset->CachePath;
                agent->Tree.Initialize(agent->Memory);
            }
            if (agent->Failed)
                continue;
            if (!agent->Done || config.Loop)
                agent->Done = agent->Tree.Tick(context) != AI::Status::Running;
            agent->Error.clear();
        } catch (const std::exception &error) {
            Impl::Cancel(*agent, Owner);
            agent->Failed = true;
            if (agent->Error != error.what()) {
                agent->Error = error.what();
                Console::Error("Behavior tree: " + agent->Error, "AI");
            }
        }
    }
}
void BehaviorAgentRuntime::Reset(Entity entity) {
    GetBlackboard(entity);
    auto agent = m_impl->Get(entity, Owner);
    auto context = Impl::Context(*agent, Owner);
    agent->Tree.Reset(context);
    agent->Done = false;
    agent->Failed = false;
}
void BehaviorAgentRuntime::SetHandlers(Entity entity, AI::ActionHandler action,
                                       AI::ConditionHandler condition, const void *owner) {
    Reset(entity);
    auto agent = m_impl->Get(entity, Owner);
    agent->Action = std::move(action);
    agent->Condition = std::move(condition);
    agent->Owner = owner;
}
void BehaviorAgentRuntime::ClearHandlers(const void *owner) {
    std::vector<std::shared_ptr<Impl::Agent>> pending;
    for (const auto &[id, agent] : m_impl->Agents)
        if (agent->Owner == owner)
            pending.push_back(agent);
    for (auto &agent : pending) {
        Impl::Cancel(*agent, Owner);
        if (agent->Owner != owner)
            continue;
        agent->Action = {};
        agent->Condition = {};
        agent->Owner = nullptr;
        agent->Done = false;
        agent->Failed = false;
    }
}
void BehaviorAgentRuntime::ResetDefaults(bool action, bool condition) {
    std::vector<std::shared_ptr<Impl::Agent>> pending;
    for (const auto &[id, agent] : m_impl->Agents)
        if ((action && !agent->Action) || (condition && !agent->Condition))
            pending.push_back(agent);
    for (auto &agent : pending) {
        Impl::Cancel(*agent, Owner);
        agent->Done = false;
        agent->Failed = false;
    }
}
void BehaviorAgentRuntime::Release(Scene &) {
    auto agents = m_impl->Agents;
    for (auto &[id, agent] : agents)
        Impl::Cancel(*agent, Owner);
    m_impl->Agents.clear();
}
} // namespace Bazzalt
