#pragma once
#include "Bazzalt/Systems/BehaviorTreeSystem.h"
namespace Bazzalt::Runtime {
struct BehaviorModuleApi {
    unsigned Version,Size;
    void* (*CreateTree)(const std::string&);
    void* (*LoadTree)(UUID);
    void (*DestroyTree)(void*) noexcept;
    AI::Status (*TickTree)(void*,AI::Context&);
    void (*ResetTree)(void*,AI::Context&);
    void (*InitializeTree)(const void*,AI::Blackboard&);
    void* (*CreateAgents)(BehaviorTreeSystem&);
    void (*DestroyAgents)(void*) noexcept;
    AI::Blackboard& (*GetBlackboard)(void*,Entity);
    void (*SetHandlers)(void*,Entity,AI::ActionHandler,AI::ConditionHandler,const void*);
    void (*ClearHandlers)(void*,const void*);
    void (*ResetAgent)(void*,Entity);
    void (*StepAgents)(void*,Scene&,float);
    void (*ReleaseAgents)(void*,Scene&);
    std::vector<UUID> (*References)(const std::string&);
    void (*ResetDefaults)(void*,bool,bool);
};
const BehaviorModuleApi& BehaviorModule();
}
