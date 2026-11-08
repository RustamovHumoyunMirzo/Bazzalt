#include "AI/BehaviorModuleApi.h"
#include "AI/BehaviorTreeRuntime.h"
#include "AI/BehaviorAgentRuntime.h"
namespace Bazzalt::Runtime {
namespace {
const BehaviorModuleApi Api{
    1,sizeof(BehaviorModuleApi),
    [](const std::string& source)->void* {return new AI::BehaviorTreeRuntime(AI::BehaviorTreeRuntime::FromSource(source));},
    [](UUID asset)->void* {return new AI::BehaviorTreeRuntime(AI::BehaviorTreeRuntime::Load(asset));},
    [](void* tree) noexcept {delete static_cast<AI::BehaviorTreeRuntime*>(tree);},
    [](void* tree,AI::Context& context){return static_cast<AI::BehaviorTreeRuntime*>(tree)->Tick(context);},
    [](void* tree,AI::Context& context){static_cast<AI::BehaviorTreeRuntime*>(tree)->Reset(context);},
    [](const void* tree,AI::Blackboard& memory){static_cast<const AI::BehaviorTreeRuntime*>(tree)->Initialize(memory);},
    [](BehaviorTreeSystem& owner)->void* {return new BehaviorAgentRuntime(owner);},
    [](void* agents) noexcept {delete static_cast<BehaviorAgentRuntime*>(agents);},
    [](void* agents,Entity entity)->AI::Blackboard& {return static_cast<BehaviorAgentRuntime*>(agents)->GetBlackboard(entity);},
    [](void* agents,Entity entity,AI::ActionHandler action,AI::ConditionHandler condition,const void* owner){static_cast<BehaviorAgentRuntime*>(agents)->SetHandlers(entity,std::move(action),std::move(condition),owner);},
    [](void* agents,const void* owner){static_cast<BehaviorAgentRuntime*>(agents)->ClearHandlers(owner);},
    [](void* agents,Entity entity){static_cast<BehaviorAgentRuntime*>(agents)->Reset(entity);},
    [](void* agents,Scene& scene,float dt){static_cast<BehaviorAgentRuntime*>(agents)->Tick(scene,dt);},
    [](void* agents,Scene& scene){static_cast<BehaviorAgentRuntime*>(agents)->Release(scene);},
    AI::BehaviorTreeRuntime::References,
    [](void* agents,bool action,bool condition){static_cast<BehaviorAgentRuntime*>(agents)->ResetDefaults(action,condition);}
};
}
}
#ifdef _WIN32
#define AI_MODULE_EXPORT __declspec(dllexport)
#else
#define AI_MODULE_EXPORT __attribute__((visibility("default")))
#endif
extern "C" AI_MODULE_EXPORT const Bazzalt::Runtime::BehaviorModuleApi*
BazzaltGetBehaviorModuleApi(unsigned version,unsigned size){
    return version==1&&size==sizeof(Bazzalt::Runtime::BehaviorModuleApi)?&Bazzalt::Runtime::Api:nullptr;
}
