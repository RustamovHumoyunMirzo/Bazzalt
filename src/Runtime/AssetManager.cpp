#include "Bazzalt/AssetManager.h"

#include "Runtime/Engine.h"

namespace Bazzalt {
namespace {
Runtime::Engine* ActiveEngine = nullptr;
}

std::optional<AssetInfo> AssetManager::GetAsset(UUID id) {
    return ActiveEngine != nullptr ? ActiveEngine->FindAsset(id) : std::nullopt;
}

std::optional<AssetInfo> AssetManager::GetAsset(const std::filesystem::path& path) {
    return ActiveEngine != nullptr ? ActiveEngine->FindAsset(path) : std::nullopt;
}

bool AssetManager::IsAssetReady(UUID id) {
    const auto asset = GetAsset(id);
    return asset && asset->State == AssetState::Ready;
}

void AssetManager::Bind(Runtime::Engine* engine) { ActiveEngine = engine; }
void AssetManager::Unbind(Runtime::Engine* engine) {
    if (ActiveEngine == engine) ActiveEngine = nullptr;
}

} // namespace Bazzalt
