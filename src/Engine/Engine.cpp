#include "Runtime/Engine.h"
#include "Runtime/AssetDatabase.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/TimeAccess.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderSystems.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/PrimitiveGeometry.h"
#include <map>
#include <tuple>
#include <limits>
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace Bazzalt::Runtime {

Engine::Engine()
    : m_scene(std::make_unique<Scene>()), m_assetDatabase(std::make_unique<AssetDatabase>()),
      m_renderBackend(std::make_unique<RenderBackend>()),m_scriptRuntime(std::make_unique<NativeScriptRuntime>())
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
    TimeAccess::Reset();
    m_fixedAccumulator = 0.0;
    if(!m_scriptRuntime->Start(m_lastError)){DetachRenderSystems();m_renderBackend->Shutdown();m_isInitialized=false;return false;}

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
    TimeAccess::Advance(std::chrono::duration<double>(currentTime - m_lastFrameTime).count());
    m_deltaTime = Time::GetDeltaTime();
    m_lastFrameTime = currentTime;

    m_frameCount++;
    m_fixedAccumulator += m_deltaTime;
    // Bound catch-up work even if game code chooses an extremely short step.
    int fixedSteps = 0;
    while(!Time::IsPaused() && m_fixedAccumulator >= Time::GetFixedDeltaTime() && fixedSteps++ < 64) {
        const float step = Time::GetFixedDeltaTime();
        m_fixedAccumulator -= step;
        TimeAccess::FixedScope fixed;
        m_scene->FixedUpdate(step);
        m_scriptRuntime->FixedUpdate(step);
    }
    if(fixedSteps > 64)m_fixedAccumulator = std::fmod(m_fixedAccumulator, double(Time::GetFixedDeltaTime()));
    m_scene->Update(m_deltaTime);
    m_scriptRuntime->Update(m_deltaTime);
    m_renderBackend->Render();
}

std::vector<std::string> Engine::SupportedRenderingBackends(){return RenderBackend::SupportedBackends();}
bool Engine::ConfigureRenderingBackend(const std::string& backend){
    if(m_renderBackend->ConfigureBackend(backend))return true;
    m_lastError="Rendering backend is unsupported by this build or requires a restart.";return false;
}

void Engine::RenderEditorFrame()
{
    if (!m_isInitialized) return;
    // Editor and paused frames must not accumulate a giant gameplay delta.
    m_lastFrameTime = std::chrono::steady_clock::now();
    ProcessPendingSceneLoad();
    m_scene->UpdateSystem<CameraSystem>();
    m_scene->UpdateSystem<LightSystem>();
    m_scene->UpdateSystem<MeshSystem>();
    m_scene->UpdateSystem<PrimitiveSystem>();
    std::vector<RenderBackend::EditorIcon> icons;
    std::vector<RenderBackend::EditorGuide> guides;
    const auto line=[&](Vec3 a,Vec3 b,Vec4 color){guides.push_back({a.X,a.Y,a.Z,b.X,b.Y,b.Z,color.X,color.Y,color.Z,color.W});};
    const auto ring=[&](Vec3 center,Vec3 axisA,Vec3 axisB,float radius,Vec4 color){constexpr int steps=32;Vec3 previous=center+axisA*radius;for(int i=1;i<=steps;++i){const float angle=2.0f*Pi*static_cast<float>(i)/steps;Vec3 next=center+(axisA*std::cos(angle)+axisB*std::sin(angle))*radius;line(previous,next,color);previous=next;}};
    auto cameras=m_scene->GetRegistry().view<Camera>();
for(auto handle:cameras){Entity entity=m_scene->GetEntity(static_cast<Entity::Id>(handle));const auto& camera=cameras.get<Camera>(handle);if(!camera.IsEnabled())continue;const Mat4 world=entity.GetWorldMatrix();const Vec3 p=world.TransformPoint({}),forward=world.TransformDirection({0,0,-1}).Normalized(),right=world.TransformDirection({1,0,0}).Normalized(),up=world.TransformDirection({0,1,0}).Normalized();icons.push_back({p.X,p.Y,p.Z,true});if(std::find(m_selectedObjects.begin(),m_selectedObjects.end(),entity.GetUUID())==m_selectedObjects.end())continue;float aspect=camera.AspectRatio;if(camera.AspectMode==CameraAspectMode::Automatic){const float width=std::max(1.0f,float(m_renderBackend->GetPresentationWidth())*camera.Viewport.Width),height=std::max(1.0f,float(m_renderBackend->GetPresentationHeight())*camera.Viewport.Height);aspect=width/height;}const float nearPlane=std::max(.001f,camera.NearPlane),farPlane=std::max(nearPlane+.001f,camera.FarPlane);float nearHeight,farHeight;if(camera.Projection==CameraProjection::Perspective){nearHeight=std::tan(camera.VerticalFieldOfView*.5f)*nearPlane;farHeight=std::tan(camera.VerticalFieldOfView*.5f)*farPlane;}else nearHeight=farHeight=std::max(.001f,camera.OrthographicSize*.5f);const float nearWidth=nearHeight*aspect,farWidth=farHeight*aspect;std::array<Vec3,4> nearCorners,farCorners;for(int i=0;i<4;++i){const float x=(i==0||i==3)?-1.0f:1.0f,y=i<2?-1.0f:1.0f;nearCorners[i]=p+forward*nearPlane+right*(x*nearWidth)+up*(y*nearHeight);farCorners[i]=p+forward*farPlane+right*(x*farWidth)+up*(y*farHeight);}const Vec4 color{.35f,.72f,1,.82f};for(int i=0;i<4;++i){line(nearCorners[i],nearCorners[(i+1)%4],color);line(farCorners[i],farCorners[(i+1)%4],color);line(camera.Projection==CameraProjection::Perspective?p:nearCorners[i],farCorners[i],color);}}
    auto lights=m_scene->GetRegistry().view<Light>();
    for(auto handle:lights){Entity entity=m_scene->GetEntity(static_cast<Entity::Id>(handle));const auto& light=lights.get<Light>(handle);if(!light.IsEnabled())continue;const Mat4 world=entity.GetWorldMatrix();const Vec3 p=world.TransformPoint({}),forward=world.TransformDirection({0,0,-1}).Normalized(),right=world.TransformDirection({1,0,0}).Normalized(),up=world.TransformDirection({0,1,0}).Normalized();icons.push_back({p.X,p.Y,p.Z,false});if(std::find(m_selectedObjects.begin(),m_selectedObjects.end(),entity.GetUUID())==m_selectedObjects.end())continue;const Vec4 color{light.Color.X,light.Color.Y,light.Color.Z,.82f};if(light.Type==LightType::Point){ring(p,right,up,light.Range,color);ring(p,right,forward,light.Range,color);ring(p,up,forward,light.Range,color);}else if(light.Type==LightType::Spot){const float length=std::max(.001f,light.Range),outer=std::tan(light.OuterConeAngle)*length,inner=std::tan(light.InnerConeAngle)*length;Vec3 end=p+forward*length;ring(end,right,up,outer,color);ring(end,right,up,inner,{color.X,color.Y,color.Z,.45f});for(const Vec3 offset:{right*outer,-right*outer,up*outer,-up*outer})line(p,end+offset,color);}else{const float length=std::max(3.0f,std::sqrt(std::max(0.0f,light.Intensity))*.1f),radius=std::tan(light.Type==LightType::Sun?light.SunAngularRadius:.03f)*length;Vec3 end=p+forward*length;ring(end,right,up,radius,color);line(p,end+right*radius,color);line(p,end-right*radius,color);line(p,end+up*radius,color);line(p,end-up*radius,color);}}
    m_renderBackend->SetEditorIcons(m_editorIconsVisible?icons:std::vector<RenderBackend::EditorIcon>{});
    if(!m_editorIconsVisible)guides.clear();
    auto outlined=m_selectedObjects;
    if(!m_hoveredObject.IsRoot()&&std::find(outlined.begin(),outlined.end(),m_hoveredObject)==outlined.end())outlined.push_back(m_hoveredObject);
    for(UUID outlineId:outlined){
    const bool selected=std::find(m_selectedObjects.begin(),m_selectedObjects.end(),outlineId)!=m_selectedObjects.end();
    const Vec4 outlineColor=selected?Vec4{.12f,.38f,1.0f,1.0f}:Vec4{.45f,.8f,1.0f,1.0f};
    Entity hovered=m_scene->GetEntity(outlineId);
    if(hovered)if(const auto* primitive=hovered.TryGetComponent<PrimitiveObject>();primitive&&primitive->IsEnabled()&&primitive->Visible){
        const auto geometry=BuildPrimitiveGeometry(*primitive);const auto world=hovered.GetWorldMatrix();
        using Point=std::tuple<int,int,int>;using Key=std::pair<Point,Point>;
        struct Edge{Vec3 A,B;bool Front=false,Back=false;int Count=0;};std::map<Key,Edge> edges;
        const auto point=[](Vec3 p){return Point{int(std::round(p.X*10000)),int(std::round(p.Y*10000)),int(std::round(p.Z*10000))};};
        for(std::size_t i=0;i+2<geometry.Indices.size();i+=3){Vec3 vertices[3];for(int j=0;j<3;++j){const auto& v=geometry.Vertices[geometry.Indices[i+j]];vertices[j]=world.TransformPoint({v.Position[0],v.Position[1],v.Position[2]});}const auto normal=Vec3::Cross(vertices[1]-vertices[0],vertices[2]-vertices[0]);if(normal.LengthSquared()<1e-12f)continue;bool front=Vec3::Dot(normal,m_hoverEye-vertices[0])>=0;for(int j=0;j<3;++j){Vec3 a=vertices[j],b=vertices[(j+1)%3];auto pa=point(a),pb=point(b);if(pb<pa){std::swap(pa,pb);std::swap(a,b);}auto& edge=edges[{pa,pb}];edge.A=a;edge.B=b;edge.Front|=front;edge.Back|=!front;++edge.Count;}}
        for(const auto& [_,edge]:edges)if(edge.Count==1||(edge.Front&&edge.Back)){line(edge.A,edge.B,outlineColor);guides.back().Outline=true;}
    }
    }
    m_renderBackend->SetEditorGuides(guides);
    m_renderBackend->Render();
}

UUID Engine::PickEditorPrimitive(Vec3 origin,Vec3 direction){
    direction=direction.Normalized();float nearest=std::numeric_limits<float>::max();UUID result{};
    auto view=m_scene->GetRegistry().view<PrimitiveObject>();
    for(auto handle:view){const auto& primitive=view.get<PrimitiveObject>(handle);if(!primitive.IsEnabled()||!primitive.Visible)continue;Entity entity=m_scene->GetEntity(static_cast<Entity::Id>(handle));Mat4 inverse;if(!entity.GetWorldMatrix().TryInverse(inverse))continue;Vec3 o=inverse.TransformPoint(origin),d=inverse.TransformDirection(direction);const auto geometry=BuildPrimitiveGeometry(primitive);
        for(std::size_t i=0;i+2<geometry.Indices.size();i+=3){Vec3 v[3];for(int j=0;j<3;++j){const auto& p=geometry.Vertices[geometry.Indices[i+j]];v[j]={p.Position[0],p.Position[1],p.Position[2]};}Vec3 e1=v[1]-v[0],e2=v[2]-v[0],p=Vec3::Cross(d,e2);float determinant=Vec3::Dot(e1,p);if(std::abs(determinant)<1e-8f)continue;float reciprocal=1/determinant;Vec3 offset=o-v[0];float u=Vec3::Dot(offset,p)*reciprocal;if(u<0||u>1)continue;Vec3 q=Vec3::Cross(offset,e1);float vcoord=Vec3::Dot(d,q)*reciprocal;if(vcoord<0||u+vcoord>1)continue;float t=Vec3::Dot(e2,q)*reciprocal;if(t>=0&&t<nearest){nearest=t;result=entity.GetUUID();}}
    }return result;
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
    m_hoverEye={eyeX,eyeY,eyeZ};
    if (m_isInitialized) m_renderBackend->SetSceneCamera(id, eyeX, eyeY, eyeZ,
                                                         targetX, targetY, targetZ);
}

void Engine::SetEditorGizmo(bool visible, float x, float y, float z, int mode) {
    m_renderBackend->SetEditorGizmo(visible, x, y, z, mode);
}

void Engine::SetEditorGizmoHover(int axis) {
    m_renderBackend->SetEditorGizmoHover(axis);
}
void Engine::SetEditorOrientationVisible(bool visible){m_renderBackend->SetEditorOrientationVisible(visible);}

void Engine::SetEditorGrid(bool visible, int plane) {
    m_renderBackend->SetEditorGrid(visible, plane);
}

bool Engine::SetSceneRenderMode(const std::string& mode) {
    return m_renderBackend&&m_renderBackend->IsInitialized()&&m_renderBackend->GetAssets().SetDebugMode(mode);
}

void Engine::Shutdown()
{
    if (!m_isInitialized)
    {
        return;
    }

    std::cout << "[Engine] Shutting down core subsystems...\n";

    m_scriptRuntime->Stop();
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

bool Engine::ConfigureScripts(std::vector<ScriptBinding> bindings){return m_scriptRuntime->Configure(std::move(bindings),m_lastError);}
bool Engine::StartScripts(){TimeAccess::Reset();m_fixedAccumulator=0.0;m_lastFrameTime=std::chrono::steady_clock::now();return m_scriptRuntime->Start(m_lastError);}
void Engine::StopScripts(){m_scriptRuntime->Stop();}

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
    m_scene->AddSystem<PrimitiveSystem>(*m_renderBackend);
}

void Engine::DetachRenderSystems()
{
    if (!m_scene || !m_renderBackend->IsInitialized()) return;
    m_scene->RemoveSystem<PrimitiveSystem>();
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
    const auto extensionBytes=path.extension().u8string();std::string extension(reinterpret_cast<const char*>(extensionBytes.data()),extensionBytes.size());
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
        const auto startupPath=path.parent_path()/m_project.StartupScene;
        if(std::filesystem::is_regular_file(startupPath)){
            if(!LoadScene(startupPath))return false;
        }else{
            // A scene can be renamed or removed outside an older editor. The
            // project must still open so the user can load/activate a replacement.
            CreateScene();m_project.StartupScene.clear();m_lastError.clear();
        }
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
