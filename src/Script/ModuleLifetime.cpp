#include "Bazzalt/Detail/ModuleLifetime.h"
#include "Bazzalt/Scene.h"
namespace Bazzalt::Detail {
namespace { thread_local std::shared_ptr<void> CurrentModule; }
std::shared_ptr<void> GetCurrentGameplayModule() { return CurrentModule; }
std::shared_ptr<void> SetCurrentGameplayModule(std::shared_ptr<void> module) {
    auto previous = std::move(CurrentModule); CurrentModule = std::move(module); return previous;
}
void RetainGameplayModule(Scene* scene) { if (scene) scene->RetainGameplayModule(); }
}
