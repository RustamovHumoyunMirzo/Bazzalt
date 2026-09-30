#include "Runtime/Engine.h"
#include "Runtime/AssetDatabase.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderSystems.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include <algorithm>
#include <cctype>
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
    m_deltaTime = 0.016f;
    m_lastFrameTime = std::chrono::steady_clock::now();

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

    const auto currentTime = std::chrono::steady_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;

    m_frameCount++;
    m_scene->Update(m_deltaTime);
    m_renderBackend->Render();
}

void Engine::RenderEditorFrame()
{
    if (!m_isInitialized) return;
    ProcessPendingSceneLoad();
    m_scene->UpdateSystem<CameraSystem>();
    m_scene->UpdateSystem<LightSystem>();
    m_scene->UpdateSystem<MeshSystem>();
    std::vector<RenderBackend::EditorIcon> icons;
    auto cameras=m_scene->GetRegistry().view<Camera>();
    for(auto handle:cameras){Entity entity=m_scene->GetEntity(static_cast<Entity::Id>(handle));const Vec3 p=entity.GetWorldMatrix().TransformPoint({});icons.push_back({p.X,p.Y,p.Z,true});}
    auto lights=m_scene->GetRegistry().view<Light>();
    for(auto handle:lights){Entity entity=m_scene->GetEntity(static_cast<Entity::Id>(handle));const Vec3 p=entity.GetWorldMatrix().TransformPoint({});icons.push_back({p.X,p.Y,p.Z,false});}
    m_renderBackend->SetEditorIcons(icons);
    m_renderBackend->Render();
}

bool Engine::CreateEditorViewport(std::uint64_t id, std::uintptr_t nativeWindow, bool scene,
                                  std::uint32_t width, std::uint32_t height, float pixelRatio) {
    if (!m_isInitialized && !Init()) return false;
    return m_renderBackend->CreateViewport(id, nativeWindow,
        scene ? RenderBackend::ViewportKind::Scene : RenderBackend::ViewportKind::Game,
        width, height, pixelRatio);
}

void Engine::ResizeEditorViewport(std::uint64_t id, std::uint32_t width, std::uint32_t height,
                                  float pixelRatio) {
    if (m_isInitialized) m_renderBackend->ResizeViewport(id, width, height, pixelRatio);
}

void Engine::DestroyEditorViewport(std::uint64_t id) {
    if (m_isInitialized) m_renderBackend->DestroyViewport(id);
}

void Engine::SetEditorCamera(std::uint64_t id, float eyeX, float eyeY, float eyeZ,
                             float targetX, float targetY, float targetZ) {
    if (m_isInitialized) m_renderBackend->SetSceneCamera(id, eyeX, eyeY, eyeZ,
                                                         targetX, targetY, targetZ);
}

void Engine::SetEditorGizmo(bool visible, float x, float y, float z, int mode) {
    m_renderBackend->SetEditorGizmo(visible, x, y, z, mode);
}

void Engine::SetEditorGizmoHover(int axis) {
    m_renderBackend->SetEditorGizmoHover(axis);
}

void Engine::SetEditorGrid(bool visible, int plane) {
    m_renderBackend->SetEditorGrid(visible, plane);
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
    DetachRenderSystems();
    m_scene = std::move(scene);
    AttachRenderSystems();
}

std::unique_ptr<Scene> Engine::TakeScene()
{
    DetachRenderSystems();
    return std::move(m_scene);
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

std::unique_ptr<Scene> Engine::LoadSceneAsset(const std::filesystem::path& path)
{
    m_lastError.clear();
    auto scene = std::make_unique<Scene>();
    if (m_sceneSerializer.Load(*scene, path)) return scene;
    m_lastError = m_sceneSerializer.GetLastError();
    return nullptr;
}

bool Engine::SaveSceneAsset(const Scene& scene, const std::filesystem::path& path)
{
    m_lastError.clear();
    if (m_sceneSerializer.Save(scene, path)) return true;
    m_lastError = m_sceneSerializer.GetLastError();
    return false;
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
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".bscene")
    {
        m_lastError = "Scene path must use the .bscene extension";
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
    m_project = std::move(project);
    m_projectPath = path;
    if (loadStartupScene && !m_project.StartupScene.empty())
    {
        if (!LoadScene(path.parent_path() / m_project.StartupScene)) return false;
    }
    else if (loadStartupScene && m_project.StartupScene.empty())
    {
        CreateScene();
        Entity camera = m_scene->CreateEntity("Main Camera");
        camera.AddComponent<Camera>();
        auto& cameraTransform = camera.GetComponent<Transform>();
        cameraTransform.Position = {0.0f, 2.0f, 6.0f};
        cameraTransform.Rotation = Quaternion::FromEuler({ToRadians(-12.0f), 0.0f, 0.0f});

        Entity sun = m_scene->CreateEntity("Sun");
        auto& light = sun.AddComponent<Light>();
        light.Type = LightType::Sun;
        light.Intensity = 100000.0f;
        sun.GetComponent<Transform>().Rotation = Quaternion::FromEuler(
            {ToRadians(-45.0f), ToRadians(-30.0f), 0.0f});

        m_project.StartupScene = m_project.AssetDirectory / "Scenes" / "Starter.bscene";
        const auto starterPath = path.parent_path() / m_project.StartupScene;
        std::error_code directoryError;
        std::filesystem::create_directories(starterPath.parent_path(), directoryError);
        if (directoryError || !SaveScene(starterPath) || !SaveProject(path)) {
            if (m_lastError.empty()) m_lastError = "Could not create the starter scene";
            return false;
        }
    }
    return true;
}

} // namespace Bazzalt::Runtime
