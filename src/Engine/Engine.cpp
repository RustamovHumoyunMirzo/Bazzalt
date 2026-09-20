#include "Runtime/Engine.h"
#include "Runtime/AssetDatabase.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderSystems.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

namespace Bazzalt::Runtime {

Engine::Engine()
    : m_scene(std::make_unique<Scene>()), m_assetDatabase(std::make_unique<AssetDatabase>()),
      m_renderBackend(std::make_unique<RenderBackend>())
{
    SceneManager::Bind(this);
    AssetManager::Bind(this);
}

Engine::~Engine()
{
    if (m_isInitialized)
    {
        Shutdown();
    }
    SceneManager::Unbind(this);
    AssetManager::Unbind(this);
}

bool Engine::Init()
{
    if (m_isInitialized)
    {
        std::cout << "[Engine] Already initialized.\n";
        return true;
    }

    std::cout << "[Engine] Initializing Core Subsystems...\n";

    if (!m_renderBackend->Initialize())
    {
        m_lastError = "Could not initialize the Filament rendering backend";
        std::cerr << "[Engine] " << m_lastError << "\n";
        return false;
    }
    AttachRenderSystems();

    m_isInitialized = true;
    m_shouldClose = false;
    m_frameCount = 0;

    std::cout << "[Engine] Initialization complete.\n";
    return true;
}

void Engine::Update()
{
    if (!m_isInitialized)
    {
        std::cerr << "[Engine] Error: Update called before Init().\n";
        return;
    }

    ProcessPendingSceneLoad();

    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;

    m_frameCount++;
    m_scene->Update(m_deltaTime);
}

void Engine::Shutdown()
{
    if (!m_isInitialized)
    {
        return;
    }

    std::cout << "[Engine] Shutting down core subsystems...\n";

    DetachRenderSystems();
    m_renderBackend->Shutdown();

    m_isInitialized = false;
    std::cout << "[Engine] Shutdown complete.\n";
}

bool Engine::ShouldClose() const
{
    return m_shouldClose;
}

void Engine::RequestClose()
{
    m_shouldClose = true;
}

Scene& Engine::CreateScene()
{
    m_scene = std::make_unique<Scene>();
    AttachRenderSystems();
    return *m_scene;
}

void Engine::SetScene(std::unique_ptr<Scene> scene)
{
    if (!scene) throw std::invalid_argument("Engine scene cannot be null");
    m_scene = std::move(scene);
    AttachRenderSystems();
}

bool Engine::SaveScene(const std::filesystem::path& path)
{
    m_lastError.clear();
    if (m_sceneSerializer.Save(*m_scene, path)) return true;
    m_lastError = m_sceneSerializer.GetLastError();
    return false;
}

bool Engine::LoadScene(const std::filesystem::path& path)
{
    m_lastError.clear();
    auto scene = std::make_unique<Scene>();
    if (!m_sceneSerializer.Load(*scene, path))
    {
        m_lastError = m_sceneSerializer.GetLastError();
        return false;
    }
    m_scene = std::move(scene);
    AttachRenderSystems();
    return true;
}

void Engine::AttachRenderSystems()
{
    if (!m_renderBackend->IsInitialized()) return;
    m_scene->AddSystem<CameraSystem>(*m_renderBackend);
    m_scene->AddSystem<LightSystem>(*m_renderBackend);
    m_scene->AddSystem<MeshSystem>(*m_renderBackend);
}

void Engine::DetachRenderSystems()
{
    if (!m_scene || !m_renderBackend->IsInitialized()) return;
    m_scene->RemoveSystem<MeshSystem>();
    m_scene->RemoveSystem<LightSystem>();
    m_scene->RemoveSystem<CameraSystem>();
}

bool Engine::RequestSceneLoad(const std::filesystem::path& path)
{
    m_lastError.clear();
    if (path.empty())
    {
        m_lastError = "Scene path cannot be empty";
        return false;
    }
    m_pendingScenePath = path;
    return true;
}

bool Engine::RequestSceneLoad(UUID assetId)
{
    const auto asset = FindAsset(assetId);
    if (!asset || asset->State != AssetState::Ready)
    {
        m_lastError = "Scene asset is not present or ready";
        return false;
    }
    if (asset->SourcePath.extension() != ".bscene")
    {
        m_lastError = "Requested asset is not a .bscene";
        return false;
    }
    return RequestSceneLoad(asset->CachePath);
}

std::optional<AssetInfo> Engine::FindAsset(UUID id) const
{
    return m_assetDatabase ? m_assetDatabase->Find(id) : std::nullopt;
}

std::optional<AssetInfo> Engine::FindAsset(const std::filesystem::path& path) const
{
    return m_assetDatabase ? m_assetDatabase->Find(path) : std::nullopt;
}

void Engine::ProcessPendingSceneLoad()
{
    if (!m_pendingScenePath) return;
    const std::filesystem::path path = std::move(*m_pendingScenePath);
    m_pendingScenePath.reset();
    LoadScene(path);
}

bool Engine::SaveProject(const std::filesystem::path& path)
{
    m_lastError.clear();
    ProjectSerializer serializer;
    if (!serializer.Save(m_project, path))
    {
        m_lastError = serializer.GetLastError();
        return false;
    }
    m_projectPath = path;
    return true;
}

bool Engine::LoadProject(const std::filesystem::path& path, bool loadStartupScene)
{
    m_lastError.clear();
    ProjectMetadata project;
    ProjectSerializer serializer;
    if (!serializer.Load(project, path))
    {
        m_lastError = serializer.GetLastError();
        return false;
    }
    if (!m_assetDatabase->Open(path.parent_path(), project.AssetDirectory))
    {
        m_lastError = m_assetDatabase->GetLastError();
        return false;
    }
    if (loadStartupScene && !project.StartupScene.empty())
    {
        if (!LoadScene(path.parent_path() / project.StartupScene)) return false;
    }
    m_project = std::move(project);
    m_projectPath = path;
    return true;
}

} // namespace Bazzalt::Runtime
