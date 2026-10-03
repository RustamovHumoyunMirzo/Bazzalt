#pragma once
#include "Bazzalt/Export.h"

#include <filesystem>
#include <optional>

#include "Bazzalt/Asset.h"
#include "Bazzalt/ModelAsset.h"

namespace Bazzalt {

namespace Runtime { class Engine; }

// Read-only game-facing access to the editor-built asset database.
class BAZZALT_API AssetManager final {
public:
    AssetManager() = delete;

    [[nodiscard]] static std::optional<AssetInfo> GetAsset(UUID id);
    [[nodiscard]] static std::optional<AssetInfo> GetAsset(const std::filesystem::path& sourcePath);
    [[nodiscard]] static bool IsAssetReady(UUID id);
    [[nodiscard]] static std::optional<ModelAsset> LoadModel(UUID id);

private:
    friend class Runtime::Engine;
    static void Bind(Runtime::Engine* engine);
    static void Unbind(Runtime::Engine* engine);
};

} // namespace Bazzalt
