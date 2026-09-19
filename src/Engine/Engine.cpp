#include "Runtime/Engine.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

namespace Bazzalt::Runtime {

Engine::Engine()
    : m_scene(std::make_unique<Scene>())
{
    SceneManager::Bind(this);
}

Engine::~Engine()
{
    if (m_isInitialized)
    {
        Shutdown();
    }
    SceneManager::Unbind(this);
}

bool Engine::Init()
{
    if (m_isInitialized)
    {
        std::cout << "[Engine] Already initialized.\n";
        return true;
    }

    std::cout << "[Engine] Initializing Core Subsystems...\n";

    // TODO: Initialize Filament, EnTT Registry, Audio, Windowing/Input here

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

    // TODO: Release Filament resources, Audio device, and window context here

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
    return *m_scene;
}

void Engine::SetScene(std::unique_ptr<Scene> scene)
{
    if (!scene) throw std::invalid_argument("Engine scene cannot be null");
    m_scene = std::move(scene);
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
    return true;
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
    if (loadStartupScene && !project.StartupScene.empty())
    {
        if (!LoadScene(path.parent_path() / project.StartupScene)) return false;
    }
    m_project = std::move(project);
    m_projectPath = path;
    return true;
}

} // namespace Bazzalt::Runtime
