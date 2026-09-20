#include "Bazzalt/AssetManager.h"

#include <algorithm>
#include <vector>

#include "Runtime/Engine.h"

namespace Bazzalt {
namespace {
std::vector<Runtime::Engine*> Engines;
Runtime::Engine* ActiveEngine() { return Engines.empty() ? nullptr : Engines.back(); }
}

std::optional<AssetInfo> AssetManager::GetAsset(UUID id) {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->FindAsset(id) : std::nullopt;
}

std::optional<AssetInfo> AssetManager::GetAsset(const std::filesystem::path& path) {
    auto* engine = ActiveEngine();
    return engine != nullptr ? engine->FindAsset(path) : std::nullopt;
}

bool AssetManager::IsAssetReady(UUID id) {
    const auto asset = GetAsset(id);
    return asset && asset->State == AssetState::Ready;
}

void AssetManager::Bind(Runtime::Engine* engine) {
    if (engine && std::find(Engines.begin(), Engines.end(), engine) == Engines.end())
        Engines.push_back(engine);
}
void AssetManager::Unbind(Runtime::Engine* engine) {
    Engines.erase(std::remove(Engines.begin(), Engines.end(), engine), Engines.end());
}

} // namespace Bazzalt
