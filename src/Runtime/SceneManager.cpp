#include "Bazzalt/SceneManager.h"

#include "Runtime/Engine.h"

namespace Bazzalt {
namespace {
Runtime::Engine* ActiveEngine = nullptr;
}

bool SceneManager::LoadScene(const std::filesystem::path& path) {
    return ActiveEngine != nullptr && ActiveEngine->RequestSceneLoad(path);
}

bool SceneManager::LoadScene(UUID assetId) {
    return ActiveEngine != nullptr && ActiveEngine->RequestSceneLoad(assetId);
}

Scene* SceneManager::GetActiveScene() {
    return ActiveEngine != nullptr ? &ActiveEngine->GetScene() : nullptr;
}

bool SceneManager::IsLoadPending() {
    return ActiveEngine != nullptr && ActiveEngine->IsSceneLoadPending();
}

std::string SceneManager::GetLastError() {
    return ActiveEngine != nullptr ? ActiveEngine->GetLastError() : "Runtime is not active";
}

void SceneManager::Bind(Runtime::Engine* engine) { ActiveEngine = engine; }
void SceneManager::Unbind(Runtime::Engine* engine) {
    if (ActiveEngine == engine) ActiveEngine = nullptr;
}

} // namespace Bazzalt
