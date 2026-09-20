#include "Bazzalt/SceneManager.h"

#include <algorithm>
#include <vector>

#include "Runtime/Engine.h"

namespace Bazzalt {
namespace {
std::vector<Runtime::Engine*> Engines;
Runtime::Engine* ActiveEngine() { return Engines.empty() ? nullptr : Engines.back(); }
}

bool SceneManager::LoadScene(const std::filesystem::path& path) {
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->RequestSceneLoad(path);
}

bool SceneManager::LoadScene(UUID assetId) {
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->RequestSceneLoad(assetId);
}

Scene* SceneManager::GetActiveScene() {
    auto* engine = ActiveEngine();
    return engine != nullptr ? &engine->GetScene() : nullptr;
}

bool SceneManager::IsLoadPending() {
    auto* engine = ActiveEngine();
    return engine != nullptr && engine->IsSceneLoadPending();
}

std::string SceneManager::GetLastError() {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->GetLastError() : "Runtime is not active";
}

void SceneManager::Bind(Runtime::Engine* engine) {
    if (engine && std::find(Engines.begin(), Engines.end(), engine) == Engines.end())
        Engines.push_back(engine);
}
void SceneManager::Unbind(Runtime::Engine* engine) {
    Engines.erase(std::remove(Engines.begin(), Engines.end(), engine), Engines.end());
}

} // namespace Bazzalt
