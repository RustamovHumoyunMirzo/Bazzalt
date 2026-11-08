#include "AI/BehaviorModuleApi.h"
#include <filesystem>
#include <stdexcept>
#include "Runtime/ModuleLoader.h"
namespace Bazzalt::Runtime {
namespace {
const BehaviorModuleApi* LoadModule(){
    const auto* api = LoadSiblingModuleApi<BehaviorModuleApi>("bazzalt_ai",
        "BazzaltGetBehaviorModuleApi", 1);
    if(!api||api->Version!=1||api->Size!=sizeof(BehaviorModuleApi)||!api->CreateTree||!api->LoadTree||!api->DestroyTree||!api->TickTree||!api->ResetTree||!api->InitializeTree||!api->CreateAgents||!api->DestroyAgents||!api->GetBlackboard||!api->SetHandlers||!api->ClearHandlers||!api->ResetAgent||!api->StepAgents||!api->ReleaseAgents||!api->References||!api->ResetDefaults)throw std::runtime_error("Missing or incompatible sibling bazzalt_ai behavior runtime");
    // Match navigation: retain the sibling module for all outstanding handles.
    return api;
}
}
const BehaviorModuleApi& BehaviorModule(){static const auto* api=LoadModule();return *api;}
}
