#pragma once
#include "Bazzalt/Export.h"

#include <filesystem>
#include <string>

#include "Bazzalt/UUID.h"
#include "Bazzalt/Streaming.h"

namespace Bazzalt {

class Scene;
namespace Runtime { class Engine; }

// Small game-facing facade. Loads are deferred to a safe frame boundary.
class BAZZALT_API SceneManager final {
public:
    SceneManager() = delete;

    static bool LoadScene(const std::filesystem::path& path);
    static bool LoadScene(UUID assetId);
    static SceneLoadHandle LoadSceneAsync(UUID assetId, bool activateOnLoad = true);
    static SceneLoadHandle LoadSceneAsync(const std::filesystem::path& path, bool activateOnLoad = true);
    static SceneLoadHandle LoadWorldCellAsync(UUID assetId, const Transform& offset = {});
    static bool UnloadWorldCell(UUID assetId);
    static bool RegisterStreamingCell(const StreamingCell& cell);
    static bool UnregisterStreamingCell(UUID assetId);
    static void SetStreamingPosition(Vec3 position);
    static StreamingSettings GetStreamingSettings();
    static void SetStreamingSettings(const StreamingSettings& settings);
    [[nodiscard]] static Scene* GetActiveScene();
    [[nodiscard]] static bool IsLoadPending();
    [[nodiscard]] static std::string GetLastError();

private:
    friend class Runtime::Engine;
    static void Bind(Runtime::Engine* engine);
    static void Unbind(Runtime::Engine* engine);
};

} // namespace Bazzalt
