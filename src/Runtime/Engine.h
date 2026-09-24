#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "Bazzalt/Project.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Serialization.h"

namespace Bazzalt::Runtime {

class AssetDatabase;
class RenderBackend;

// Runtime-owned orchestration. Kept outside the public include tree so only
// the editor/runtime host can control initialization and frame flow.
class Engine final {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Init();
    void Update();
    void RenderEditorFrame();
    void Shutdown();
    [[nodiscard]] bool ShouldClose() const;
    void RequestClose();
    [[nodiscard]] float GetDeltaTime() const { return m_deltaTime; }
    [[nodiscard]] std::uint64_t GetFrameCount() const { return m_frameCount; }

    [[nodiscard]] Scene& GetScene() { return *m_scene; }
    [[nodiscard]] const Scene& GetScene() const { return *m_scene; }
    Scene& CreateScene();
    void SetScene(std::unique_ptr<Scene> scene);

    bool SaveScene(const std::filesystem::path& path);
    bool LoadScene(const std::filesystem::path& path);
    bool SaveProject(const std::filesystem::path& path);
    bool LoadProject(const std::filesystem::path& path, bool loadStartupScene = true);

    [[nodiscard]] SceneSerializer& GetSceneSerializer() { return m_sceneSerializer; }
    [[nodiscard]] ProjectMetadata& GetProject() { return m_project; }
    [[nodiscard]] const ProjectMetadata& GetProject() const { return m_project; }
    [[nodiscard]] const std::string& GetLastError() const { return m_lastError; }
    bool CreateEditorViewport(std::uint64_t id, std::uintptr_t nativeWindow, bool scene,
                              std::uint32_t width, std::uint32_t height);
    void ResizeEditorViewport(std::uint64_t id, std::uint32_t width, std::uint32_t height);
    void DestroyEditorViewport(std::uint64_t id);
    void SetEditorCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                         float targetX, float targetY, float targetZ);
    void SetEditorGizmo(bool visible, float x, float y, float z, int mode);
    void SetEditorGizmoHover(int axis);
    void SetEditorGrid(bool visible, int plane);

private:
    friend class Bazzalt::SceneManager;
    friend class Bazzalt::AssetManager;

    bool RequestSceneLoad(const std::filesystem::path& path);
    bool RequestSceneLoad(UUID assetId);
    [[nodiscard]] std::optional<AssetInfo> FindAsset(UUID id) const;
    [[nodiscard]] std::optional<AssetInfo> FindAsset(const std::filesystem::path& path) const;
    [[nodiscard]] bool IsSceneLoadPending() const { return m_pendingScenePath.has_value(); }
    void ProcessPendingSceneLoad();
    void AttachRenderSystems();
    void DetachRenderSystems();

    bool m_isInitialized = false;
    bool m_shouldClose = false;
    float m_deltaTime = 0.016f;
    std::uint64_t m_frameCount = 0;
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::unique_ptr<Scene> m_scene;
    SceneSerializer m_sceneSerializer;
    ProjectMetadata m_project;
    std::filesystem::path m_projectPath;
    std::string m_lastError;
    std::optional<std::filesystem::path> m_pendingScenePath;
    std::unique_ptr<AssetDatabase> m_assetDatabase;
    std::unique_ptr<RenderBackend> m_renderBackend;
};

} // namespace Bazzalt::Runtime
