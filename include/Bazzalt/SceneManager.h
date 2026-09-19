#pragma once

#include <filesystem>
#include <string>

namespace Bazzalt {

class Scene;
namespace Runtime { class Engine; }

// Small game-facing facade. Loads are deferred to a safe frame boundary.
class SceneManager final {
public:
    SceneManager() = delete;

    static bool LoadScene(const std::filesystem::path& path);
    [[nodiscard]] static Scene* GetActiveScene();
    [[nodiscard]] static bool IsLoadPending();
    [[nodiscard]] static std::string GetLastError();

private:
    friend class Runtime::Engine;
    static void Bind(Runtime::Engine* engine);
    static void Unbind(Runtime::Engine* engine);
};

} // namespace Bazzalt
