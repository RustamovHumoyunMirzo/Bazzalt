#include <filesystem>
#include <fstream>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/ModelInstance.h"
#include "Bazzalt/Components/ModelNode.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Bazzalt/Components/SceneQueryBounds.h"

namespace py = pybind11;

namespace Bazzalt::EditorBridge {
namespace {

UUID ParseUuid(const std::string& text) {
    UUID result;
    if (!UUID::TryParse(text, result)) throw std::invalid_argument("Invalid UUID");
    return result;
}

py::str PathText(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring value = path.wstring();
    return py::reinterpret_steal<py::str>(PyUnicode_FromWideChar(value.c_str(),
        static_cast<Py_ssize_t>(value.size())));
#else
    return py::str(path.string());
#endif
}

py::dict SnapshotEntity(Scene& scene, Entity entity) {
    py::dict result;
    result["uuid"] = entity.GetUUID().ToString();
    result["name"] = entity.GetComponent<Name>().Value;
    const Entity parent = entity.GetParent();
    result["parent"] = parent ? parent.GetUUID().ToString() : std::string{};
    const Transform transform = entity.GetWorldTransform();
    result["position"] = py::make_tuple(transform.Position.X, transform.Position.Y, transform.Position.Z);
    result["rotation"] = py::make_tuple(transform.Rotation.X, transform.Rotation.Y,
                                          transform.Rotation.Z, transform.Rotation.W);
    result["scale"] = py::make_tuple(transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
    const Vec3 worldPosition = entity.GetWorldMatrix().TransformPoint({});
    result["world_position"] = py::make_tuple(worldPosition.X, worldPosition.Y, worldPosition.Z);
    py::list components;
    components.append("Transform");
    if (entity.HasComponent<Camera>()) components.append("Camera");
    if (entity.HasComponent<Light>()) components.append("Light");
    if (entity.HasComponent<Mesh>()) components.append("Mesh");
    if (entity.HasComponent<ModelInstance>()) components.append("Model Instance");
    if (entity.HasComponent<ModelNode>()) components.append("Model Node");
    if (entity.HasComponent<GaussianBlur>()) components.append("Gaussian Blur");
    if (entity.HasComponent<Vignette>()) components.append("Vignette");
    if (entity.HasComponent<SceneQueryBounds>()) components.append("Scene Query Bounds");
    result["components"] = components;
    py::dict enabled;
    if (const auto* value=entity.TryGetComponent<Camera>()) enabled["Camera"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Light>()) enabled["Light"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Mesh>()) enabled["Mesh"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<ModelInstance>()) enabled["Model Instance"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<ModelNode>()) enabled["Model Node"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<GaussianBlur>()) enabled["Gaussian Blur"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Vignette>()) enabled["Vignette"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<SceneQueryBounds>()) enabled["Scene Query Bounds"]=value->IsEnabled();
    result["component_enabled"] = enabled;
    py::dict data;
    if (const auto* camera=entity.TryGetComponent<Camera>()) {
        py::dict v;v["Projection"]=static_cast<int>(camera->Projection);v["Field of View"]=ToDegrees(camera->VerticalFieldOfView);v["Orthographic Size"]=camera->OrthographicSize;v["Near"]=camera->NearPlane;v["Far"]=camera->FarPlane;v["Aspect Ratio"]=camera->AspectRatio;v["Aspect Mode"]=static_cast<int>(camera->AspectMode);v["Viewport"]=py::make_tuple(camera->Viewport.X,camera->Viewport.Y,camera->Viewport.Width,camera->Viewport.Height);v["Priority"]=camera->Priority;v["Active"]=camera->Active;v["Clear Color"]=py::make_tuple(camera->ClearColor.X,camera->ClearColor.Y,camera->ClearColor.Z,camera->ClearColor.W);v["Post Processing"]=camera->PostProcessing.Enabled;v["Bloom"]=camera->PostProcessing.Bloom;v["Ambient Occlusion"]=camera->PostProcessing.AmbientOcclusion;v["Anti Aliasing"]=static_cast<int>(camera->PostProcessing.AntiAliasingMode);v["Tone Mapping"]=static_cast<int>(camera->PostProcessing.ToneMappingMode);v["Exposure"]=camera->PostProcessing.Exposure;v["Depth of Field"]=camera->PostProcessing.DepthOfField.Enabled;v["Focus Distance"]=camera->PostProcessing.DepthOfField.FocusDistance;v["Aperture"]=camera->PostProcessing.DepthOfField.Aperture;v["Shutter Speed"]=camera->PostProcessing.DepthOfField.ShutterSpeed;v["Sensitivity"]=camera->PostProcessing.DepthOfField.Sensitivity;data["Camera"]=v;
    }
    if (const auto* light=entity.TryGetComponent<Light>()) {
        py::dict v;v["Type"]=static_cast<int>(light->Type);v["Color"]=py::make_tuple(light->Color.X,light->Color.Y,light->Color.Z);v["Intensity"]=light->Intensity;v["Range"]=light->Range;v["Inner Cone"]=ToDegrees(light->InnerConeAngle);v["Outer Cone"]=ToDegrees(light->OuterConeAngle);v["Sun Angular Radius"]=light->SunAngularRadius;v["Sun Halo Size"]=light->SunHaloSize;v["Sun Halo Falloff"]=light->SunHaloFalloff;v["Cast Shadows"]=light->CastShadows;data["Light"]=v;
    }
    if (const auto* mesh=entity.TryGetComponent<Mesh>()) { py::dict v;v["Mesh Asset"]=mesh->MeshAsset.ToString();v["Model Node Index"]=mesh->ModelNodeIndex;v["Material Count"]=static_cast<int>(mesh->Materials.size());v["Layer Mask"]=mesh->LayerMask;v["Visible"]=mesh->Visible;v["Cast Shadows"]=mesh->CastShadows;v["Receive Shadows"]=mesh->ReceiveShadows;data["Mesh"]=v; }
    if (const auto* model=entity.TryGetComponent<ModelInstance>()){py::dict v;v["Model Asset"]=model->ModelAsset.ToString();data["Model Instance"]=v;}
    if (const auto* node=entity.TryGetComponent<ModelNode>()){py::dict v;v["Model Asset"]=node->ModelAsset.ToString();v["Source Index"]=node->SourceIndex;v["Mesh Index"]=node->MeshIndex;v["Stable Path"]=node->StablePath;v["Has Mesh"]=node->HasMesh;data["Model Node"]=v;}
    if (const auto* blur=entity.TryGetComponent<GaussianBlur>()) { py::dict v;v["Size"]=blur->Size;data["Gaussian Blur"]=v; }
    if (const auto* vignette=entity.TryGetComponent<Vignette>()) { py::dict v;v["Color"]=py::make_tuple(vignette->Color.X,vignette->Color.Y,vignette->Color.Z,vignette->Color.W);v["Intensity"]=vignette->Intensity;v["Smoothness"]=vignette->Smoothness;v["Roundness"]=vignette->Roundness;data["Vignette"]=v; }
    if (const auto* bounds=entity.TryGetComponent<SceneQueryBounds>()) { py::dict v;v["Shape"]=static_cast<int>(bounds->Shape);v["Center"]=py::make_tuple(bounds->Center.X,bounds->Center.Y,bounds->Center.Z);v["Extents"]=py::make_tuple(bounds->Extents.X,bounds->Extents.Y,bounds->Extents.Z);v["Radius"]=bounds->Radius;v["Layer Mask"]=bounds->LayerMask;data["Scene Query Bounds"]=v; }
    result["component_data"] = data;
    return result;
}

class EditorHost final {
public:
    EditorHost() : m_engine(std::make_unique<Runtime::Engine>()) { m_loadedScenes.push_back({{},nullptr}); }
    ~EditorHost() { Stop(); ClearLoadedScenes(); m_engine->Shutdown(); CleanupSnapshot(); }

    bool LoadProject(const std::string& path) {
        Stop();m_bridgeError.clear();bool result=false;
        try { result=m_engine->LoadProject(std::filesystem::u8path(path),true); }
        catch(const std::filesystem::filesystem_error& error){m_bridgeError="Filesystem error while loading the project: "+std::string(error.what());return false;}
        catch(const std::exception& error){m_bridgeError="Native error while loading the project: "+std::string(error.what());return false;}
        if (result) {
            m_projectPath = std::filesystem::u8path(path);
            m_scenePath = m_engine->GetProject().StartupScene.empty() ? std::filesystem::path{} :
                m_projectPath.parent_path() / m_engine->GetProject().StartupScene;
            ClearLoadedScenes();
            m_loadedScenes.push_back({m_scenePath, nullptr});
            m_activeScene = 0;
            AcquireSceneLock(m_loadedScenes[0]);
        }
        return result;
    }
    bool LoadScene(const std::string& path) { Stop(); const bool ok=m_engine->LoadScene(std::filesystem::u8path(path));if(ok){m_scenePath=std::filesystem::weakly_canonical(std::filesystem::u8path(path));ClearLoadedScenes();m_loadedScenes.push_back({m_scenePath,nullptr});m_activeScene=0;AcquireSceneLock(m_loadedScenes[0]);}return ok; }
    bool LoadSceneAdditive(const std::string& value) {
        const auto path=std::filesystem::weakly_canonical(std::filesystem::u8path(value));
        for(const auto& loaded:m_loadedScenes)if(!loaded.Path.empty()&&std::filesystem::weakly_canonical(loaded.Path)==path)return true;
        auto scene=m_engine->LoadSceneAsset(path);
        if(!scene)return false;
        for(auto& loaded:m_loadedScenes)if(SceneFor(loaded).GetUUID()==scene->GetUUID()){
            if(!loaded.Path.empty()&&!std::filesystem::exists(loaded.Path)){ReleaseSceneLock(loaded);loaded.Path=path;AcquireSceneLock(loaded);}return true;
        }
        m_loadedScenes.push_back({path,std::move(scene)});AcquireSceneLock(m_loadedScenes.back());return true;
    }
    bool ActivateScene(const std::string& id) {
        const auto target=FindLoadedScene(id);if(!target||*target==m_activeScene)return target.has_value();
        Stop();m_loadedScenes[m_activeScene].Data=m_engine->TakeScene();
        m_engine->SetScene(std::move(m_loadedScenes[*target].Data));m_activeScene=*target;
        m_scenePath=m_loadedScenes[m_activeScene].Path;UpdateStartupScene();return true;
    }
    bool UnloadScene(const std::string& id) {
        const auto target=FindLoadedScene(id);if(!target||*target==m_activeScene)return false;
        ReleaseSceneLock(m_loadedScenes[*target]);
        if(!m_loadedScenes[*target].Path.empty()&&!m_engine->SaveSceneAsset(*m_loadedScenes[*target].Data,m_loadedScenes[*target].Path)){AcquireSceneLock(m_loadedScenes[*target]);return false;}
        m_loadedScenes.erase(m_loadedScenes.begin()+static_cast<std::ptrdiff_t>(*target));
        if(*target<m_activeScene)--m_activeScene;return true;
    }
    bool SaveScene(const std::string& path) { bool ok=true;for(auto& loaded:m_loadedScenes)ReleaseSceneLock(loaded);ok=m_engine->SaveScene(std::filesystem::u8path(path));if(ok){m_scenePath=std::filesystem::u8path(path);if(m_activeScene<m_loadedScenes.size())m_loadedScenes[m_activeScene].Path=m_scenePath;for(std::size_t i=0;i<m_loadedScenes.size()&&ok;++i)if(i!=m_activeScene&&!m_loadedScenes[i].Path.empty())ok=m_engine->SaveSceneAsset(*m_loadedScenes[i].Data,m_loadedScenes[i].Path);}for(auto& loaded:m_loadedScenes)AcquireSceneLock(loaded);return ok; }
    void NewScene() { Stop(); m_engine->CreateScene();m_scenePath.clear();ClearLoadedScenes();m_loadedScenes.push_back({{},nullptr});m_activeScene=0; }
    bool RenameLoadedScene(const std::string& idOrPath,const std::string& requestedName) {
        const auto found=FindLoadedScene(idOrPath);if(!found)return false;auto& loaded=m_loadedScenes[*found];if(loaded.Path.empty())return false;
        auto filename=std::filesystem::u8path(requestedName).filename();if(filename.extension() != ".bscene")filename += ".bscene";
        const auto target=loaded.Path.parent_path()/filename;if(target==loaded.Path)return true;if(std::filesystem::exists(target))return false;
        const auto old=loaded.Path;ReleaseSceneLock(loaded);std::error_code error;std::filesystem::rename(old,target,error);
        if(error){AcquireSceneLock(loaded);return false;}auto oldMeta=old;oldMeta += ".meta";auto targetMeta=target;targetMeta += ".meta";if(std::filesystem::exists(oldMeta))std::filesystem::rename(oldMeta,targetMeta,error);
        loaded.Path=target;if(*found==m_activeScene){m_scenePath=target;UpdateStartupScene();}AcquireSceneLock(loaded);return true;
    }
    bool IsSceneLoaded(const std::string& path) { return FindLoadedScene(path).has_value(); }
    std::string LastError() const { return m_bridgeError.empty()?m_engine->GetLastError():m_bridgeError; }
    py::str ProjectDirectory() const { return m_projectPath.empty() ? py::str() : PathText(m_projectPath.parent_path()); }
    py::str AssetDirectory() const {
        if (m_projectPath.empty()) return py::str();
        return PathText(m_projectPath.parent_path() / m_engine->GetProject().AssetDirectory);
    }

    py::list SceneEntities(Scene& scene) {
        py::list result;
        std::function<void(Entity)> append = [&](Entity parent) {
            for (Entity child : parent.GetChildren()) {
                result.append(SnapshotEntity(scene, child));
                append(child);
            }
        };
        append(scene.GetRootEntity());
        return result;
    }
    py::list Entities() { return SceneEntities(m_engine->GetScene()); }
    py::list LoadedScenes() {
        py::list result;
        for(std::size_t index=0;index<m_loadedScenes.size();++index){Scene& scene=SceneAt(index);py::dict item;item["uuid"]=scene.GetUUID().ToString();item["name"]=m_loadedScenes[index].Path.empty()?py::str("Untitled"):PathText(m_loadedScenes[index].Path.stem());item["path"]=m_loadedScenes[index].Path.empty()?py::str():PathText(m_loadedScenes[index].Path);item["active"]=index==m_activeScene;item["entities"]=SceneEntities(scene);result.append(item);}return result;
    }
    py::dict EntityDetails(const std::string& id) {
        auto [scene,entity]=FindEntity(id);if(!entity)return {};auto result=SnapshotEntity(*scene,entity);result["scene_active"]=(scene==&m_engine->GetScene());result["scene_uuid"]=scene->GetUUID().ToString();return result;
    }
    bool HasActiveCamera() const {
        const auto cameras = m_engine->GetScene().GetRegistry().view<Camera>();
        for (const auto handle : cameras)
            if (const auto& camera=cameras.get<Camera>(handle);
                camera.Active && camera.IsEnabled()) return true;
        return false;
    }
    std::string CreateEntity(const std::string& name, const std::string& parent) {
        Scene* scene=&m_engine->GetScene();Entity parentEntity;
        if(!parent.empty()){if(const auto loaded=FindLoadedScene(parent))scene=&SceneAt(*loaded);else{auto found=FindEntity(parent);scene=found.first;parentEntity=found.second;if(!scene||!parentEntity)return {};}}
        Entity entity = scene->CreateEntity(name);
        if (parentEntity) entity.SetParent(parentEntity);
        return entity.GetUUID().ToString();
    }
    bool DestroyEntity(const std::string& id) {
        auto [scene,entity]=FindEntity(id);if(!scene||!entity)return false;scene->DestroyEntity(entity);return true;
    }
    bool SetParent(const std::string& id, const std::string& parent) {
        auto [scene,entity]=FindEntity(id);if(!scene||!entity)return false;
        if(parent.empty())return entity.SetParent(scene->GetRootEntity());
        if(const auto loaded=FindLoadedScene(parent))return &SceneAt(*loaded)==scene&&entity.SetParent(scene->GetRootEntity());
        auto [parentScene,parentEntity]=FindEntity(parent);return parentScene==scene&&parentEntity&&entity.SetParent(parentEntity);
    }
    bool Rename(const std::string& id, const std::string& name) {
        RequireEntity(id).GetComponent<Name>().Value = name; return true;
    }
    bool SetTransform(const std::string& id, const std::array<float, 3>& position,
                      const std::array<float, 4>& rotation,
                      const std::array<float, 3>& scale) {
        Transform value;
        value.Position = {position[0], position[1], position[2]};
        value.Rotation = Quaternion{rotation[0], rotation[1], rotation[2], rotation[3]}.Normalized();
        value.Scale = {scale[0], scale[1], scale[2]};
        return RequireEntity(id).SetWorldTransform(value);
    }
    bool Translate(const std::string& id, const std::array<float, 3>& delta) {
        Entity entity=RequireEntity(id);Transform value=entity.GetWorldTransform();
        value.Translate({delta[0], delta[1], delta[2]});return entity.SetWorldTransform(value);
    }
    std::string InstantiateModel(const std::string& asset, const std::string& parent) {
        Entity entity = m_engine->GetScene().InstantiateModel(
            ParseUuid(asset), parent.empty() ? Entity{} : RequireEntity(parent));
        return entity.GetUUID().ToString();
    }
    std::string InstantiateModelPath(const std::string& path, const std::string& parent) {
        const auto asset=AssetManager::GetAsset(std::filesystem::u8path(path));
        if(!asset)return {};
        Scene* scene=&m_engine->GetScene();Entity parentEntity;
        if(!parent.empty()){if(const auto loaded=FindLoadedScene(parent))scene=&SceneAt(*loaded);else{auto found=FindEntity(parent);scene=found.first;parentEntity=found.second;if(!scene||!parentEntity)return {};}}
        Entity entity=scene->InstantiateModel(asset->Id,parentEntity);
        return entity?entity.GetUUID().ToString():std::string{};
    }

    bool AddComponent(const std::string& id, const std::string& type) {
        Entity entity = RequireEntity(id);
        if (type == "Camera") { if (!entity.HasComponent<Camera>()) entity.AddComponent<Camera>(); }
        else if (type == "Light") { if (!entity.HasComponent<Light>()) entity.AddComponent<Light>(); }
        else if (type == "Mesh") { if (!entity.HasComponent<Mesh>()) entity.AddComponent<Mesh>(); }
        else if (type == "Gaussian Blur") { if (!entity.HasComponent<GaussianBlur>()) entity.AddComponent<GaussianBlur>(); }
        else if (type == "Vignette") { if (!entity.HasComponent<Vignette>()) entity.AddComponent<Vignette>(); }
        else if (type == "Scene Query Bounds") { if (!entity.HasComponent<SceneQueryBounds>()) entity.AddComponent<SceneQueryBounds>(); }
        else return false;
        return true;
    }
    bool RemoveComponent(const std::string& id, const std::string& type) {
        Entity entity = RequireEntity(id);
        if (type == "Camera" && entity.HasComponent<Camera>()) entity.RemoveComponent<Camera>();
        else if (type == "Light" && entity.HasComponent<Light>()) entity.RemoveComponent<Light>();
        else if (type == "Mesh" && entity.HasComponent<Mesh>()) entity.RemoveComponent<Mesh>();
        else if (type == "Gaussian Blur" && entity.HasComponent<GaussianBlur>()) entity.RemoveComponent<GaussianBlur>();
        else if (type == "Vignette" && entity.HasComponent<Vignette>()) entity.RemoveComponent<Vignette>();
        else if (type == "Scene Query Bounds" && entity.HasComponent<SceneQueryBounds>()) entity.RemoveComponent<SceneQueryBounds>();
        else return false;
        return true;
    }
    bool SetComponentEnabled(const std::string& id, const std::string& type, bool enabled) {
        Entity entity = RequireEntity(id);
        if (type == "Camera" && entity.HasComponent<Camera>()) entity.SetComponentEnabled<Camera>(enabled);
        else if (type == "Light" && entity.HasComponent<Light>()) entity.SetComponentEnabled<Light>(enabled);
        else if (type == "Mesh" && entity.HasComponent<Mesh>()) entity.SetComponentEnabled<Mesh>(enabled);
        else if (type == "Model Instance" && entity.HasComponent<ModelInstance>()) entity.SetComponentEnabled<ModelInstance>(enabled);
        else if (type == "Model Node" && entity.HasComponent<ModelNode>()) entity.SetComponentEnabled<ModelNode>(enabled);
        else if (type == "Gaussian Blur" && entity.HasComponent<GaussianBlur>()) entity.SetComponentEnabled<GaussianBlur>(enabled);
        else if (type == "Vignette" && entity.HasComponent<Vignette>()) entity.SetComponentEnabled<Vignette>(enabled);
        else if (type == "Scene Query Bounds" && entity.HasComponent<SceneQueryBounds>()) entity.SetComponentEnabled<SceneQueryBounds>(enabled);
        else return false;
        return true;
    }
    py::list ComponentTypes() const {
        py::list result;
        for (const char* name : {"Camera", "Light", "Mesh", "Scene Query Bounds", "Gaussian Blur", "Vignette"})
            result.append(name);
        return result;
    }
    py::dict SceneInfo() const {
        py::dict result;
        result["uuid"] = m_engine->GetScene().GetUUID().ToString();
        result["name"] = m_scenePath.empty() ? py::str("Untitled") : PathText(m_scenePath.stem());
        result["path"] = m_scenePath.empty() ? py::str() : PathText(m_scenePath);
        return result;
    }
    py::bytes CaptureScene() {
        const auto path=std::filesystem::temp_directory_path()/("bazzalt-history-"+UUID::Generate().ToString()+".bscene");
        if(!m_engine->SaveScene(path))return {};
        std::ifstream stream(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(stream)),{});
        std::error_code error;std::filesystem::remove(path,error);return py::bytes(data);
    }
    bool RestoreScene(const py::bytes& snapshot) {
        const std::string data=snapshot;
        if(data.empty())return false;
        const auto path=std::filesystem::temp_directory_path()/("bazzalt-history-"+UUID::Generate().ToString()+".bscene");
        {std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(data.data(),static_cast<std::streamsize>(data.size()));if(!stream){std::error_code error;std::filesystem::remove(path,error);return false;}}
        const bool result=m_engine->LoadScene(path);std::error_code error;std::filesystem::remove(path,error);return result;
    }
    bool SetComponentProperty(const std::string& id, const std::string& type,
                              const std::string& property, py::object value) {
        Entity entity=RequireEntity(id);
        if(type=="Camera"&&entity.HasComponent<Camera>()){auto&v=entity.GetComponent<Camera>();if(property=="Projection")v.Projection=static_cast<CameraProjection>(value.cast<int>());else if(property=="Field of View")v.VerticalFieldOfView=ToRadians(value.cast<float>());else if(property=="Orthographic Size")v.OrthographicSize=value.cast<float>();else if(property=="Near")v.NearPlane=value.cast<float>();else if(property=="Far")v.FarPlane=value.cast<float>();else if(property=="Aspect Ratio")v.AspectRatio=value.cast<float>();else if(property=="Aspect Mode")v.AspectMode=static_cast<CameraAspectMode>(value.cast<int>());else if(property=="Viewport"){auto a=value.cast<std::array<float,4>>();v.Viewport={a[0],a[1],a[2],a[3]};}else if(property=="Priority")v.Priority=value.cast<int>();else if(property=="Active")v.Active=value.cast<bool>();else if(property=="Clear Color"){auto a=value.cast<std::array<float,4>>();v.ClearColor={a[0],a[1],a[2],a[3]};}else if(property=="Post Processing")v.PostProcessing.Enabled=value.cast<bool>();else if(property=="Bloom")v.PostProcessing.Bloom=value.cast<bool>();else if(property=="Ambient Occlusion")v.PostProcessing.AmbientOcclusion=value.cast<bool>();else if(property=="Anti Aliasing")v.PostProcessing.AntiAliasingMode=static_cast<AntiAliasing>(value.cast<int>());else if(property=="Tone Mapping")v.PostProcessing.ToneMappingMode=static_cast<ToneMapping>(value.cast<int>());else if(property=="Exposure")v.PostProcessing.Exposure=value.cast<float>();else if(property=="Depth of Field")v.PostProcessing.DepthOfField.Enabled=value.cast<bool>();else if(property=="Focus Distance")v.PostProcessing.DepthOfField.FocusDistance=value.cast<float>();else if(property=="Aperture")v.PostProcessing.DepthOfField.Aperture=value.cast<float>();else if(property=="Shutter Speed")v.PostProcessing.DepthOfField.ShutterSpeed=value.cast<float>();else if(property=="Sensitivity")v.PostProcessing.DepthOfField.Sensitivity=value.cast<float>();else return false;return true;}
        if(type=="Light"&&entity.HasComponent<Light>()){auto&v=entity.GetComponent<Light>();if(property=="Type")v.Type=static_cast<LightType>(value.cast<int>());else if(property=="Color"){auto a=value.cast<std::array<float,3>>();v.Color={a[0],a[1],a[2]};}else if(property=="Intensity")v.Intensity=value.cast<float>();else if(property=="Range")v.Range=value.cast<float>();else if(property=="Inner Cone")v.InnerConeAngle=ToRadians(value.cast<float>());else if(property=="Outer Cone")v.OuterConeAngle=ToRadians(value.cast<float>());else if(property=="Sun Angular Radius")v.SunAngularRadius=value.cast<float>();else if(property=="Sun Halo Size")v.SunHaloSize=value.cast<float>();else if(property=="Sun Halo Falloff")v.SunHaloFalloff=value.cast<float>();else if(property=="Cast Shadows")v.CastShadows=value.cast<bool>();else if(property=="Enabled")v.Enabled=value.cast<bool>();else return false;return true;}
        if(type=="Mesh"&&entity.HasComponent<Mesh>()){auto&v=entity.GetComponent<Mesh>();if(property=="Mesh Asset"){const auto text=value.cast<std::string>();UUID id;if(!UUID::TryParse(text,id)){const auto asset=AssetManager::GetAsset(std::filesystem::u8path(text));if(!asset)return false;id=asset->Id;}v.MeshAsset=id;}else if(property=="Model Node Index")v.ModelNodeIndex=value.cast<std::uint32_t>();else if(property=="Visible")v.Visible=value.cast<bool>();else if(property=="Cast Shadows")v.CastShadows=value.cast<bool>();else if(property=="Receive Shadows")v.ReceiveShadows=value.cast<bool>();else if(property=="Layer Mask")v.LayerMask=static_cast<std::uint8_t>(value.cast<int>());else return false;return true;}
        if(type=="Gaussian Blur"&&entity.HasComponent<GaussianBlur>()){auto&v=entity.GetComponent<GaussianBlur>();if(property=="Enabled")v.Enabled=value.cast<bool>();else if(property=="Size")v.Size=value.cast<float>();else return false;return true;}
        if(type=="Vignette"&&entity.HasComponent<Vignette>()){auto&v=entity.GetComponent<Vignette>();if(property=="Enabled")v.Enabled=value.cast<bool>();else if(property=="Color"){auto a=value.cast<std::array<float,4>>();v.Color={a[0],a[1],a[2],a[3]};}else if(property=="Intensity")v.Intensity=value.cast<float>();else if(property=="Smoothness")v.Smoothness=value.cast<float>();else if(property=="Roundness")v.Roundness=value.cast<float>();else return false;return true;}
        if(type=="Scene Query Bounds"&&entity.HasComponent<SceneQueryBounds>()){auto&v=entity.GetComponent<SceneQueryBounds>();if(property=="Shape")v.Shape=static_cast<SceneQueryShape>(value.cast<int>());else if(property=="Center"){auto a=value.cast<std::array<float,3>>();v.Center={a[0],a[1],a[2]};}else if(property=="Extents"){auto a=value.cast<std::array<float,3>>();v.Extents={a[0],a[1],a[2]};}else if(property=="Radius")v.Radius=value.cast<float>();else if(property=="Layer Mask")v.LayerMask=value.cast<std::uint32_t>();else if(property=="Enabled")v.Enabled=value.cast<bool>();else return false;return true;}
        return false;
    }
    void SetGizmo(const std::string& id, int mode) {
        if(id.empty()){m_engine->SetEditorGizmo(false,0,0,0,0);return;}
        Entity entity=m_engine->GetScene().GetEntity(ParseUuid(id));
        if(!entity||mode==0){m_engine->SetEditorGizmo(false,0,0,0,0);return;}
        Vec3 p=entity.GetWorldMatrix().TransformPoint({});m_engine->SetEditorGizmo(true,p.X,p.Y,p.Z,mode);
    }
    void SetGizmoPosition(const std::array<float,3>& position,int mode) {
        m_engine->SetEditorGizmo(mode!=0,position[0],position[1],position[2],mode);
    }
    void SetGizmoHover(int axis) { m_engine->SetEditorGizmoHover(axis); }
    void SetGrid(bool visible, int plane) { m_engine->SetEditorGrid(visible, plane); }
    bool CreateViewport(std::uint64_t id, std::uintptr_t handle, bool scene,
                        std::uint32_t width, std::uint32_t height, float pixelRatio) {
        return m_engine->CreateEditorViewport(id, handle, scene, width, height, pixelRatio);
    }
    void ResizeViewport(std::uint64_t id, std::uint32_t width, std::uint32_t height,
                        float pixelRatio) {
        m_engine->ResizeEditorViewport(id, width, height, pixelRatio);
    }
    void DestroyViewport(std::uint64_t id) { m_engine->DestroyEditorViewport(id); }
    void SetSceneCamera(std::uint64_t id, const std::array<float, 3>& eye,
                        const std::array<float, 3>& target) {
        m_engine->SetEditorCamera(id, eye[0], eye[1], eye[2], target[0], target[1], target[2]);
    }

    bool Play() {
        if (m_playing) return true;
        CleanupSnapshot();
        m_snapshot = std::filesystem::temp_directory_path() /
            ("bazzalt-editor-play-" + UUID::Generate().ToString() + ".bscene");
        if (!m_engine->SaveScene(m_snapshot)) return false;
        if (!m_engine->Init()||!m_engine->StartScripts()) { CleanupSnapshot(); return false; }
        m_playing = true; m_paused = false; return true;
    }
    void Pause(bool paused) { if (m_playing) m_paused = paused; }
    void Step() { if (m_playing) m_engine->Update(); }
    void Tick() {
        if (m_playing && !m_paused) m_engine->Update();
        else m_engine->RenderEditorFrame();
    }
    void Stop() {
        if (!m_playing) return;
        m_playing = false; m_paused = false;
        m_engine->StopScripts();
        if (!m_snapshot.empty()) m_engine->LoadScene(m_snapshot);
        CleanupSnapshot();
    }
    bool IsPlaying() const { return m_playing; }
    bool IsPaused() const { return m_paused; }

    bool ConfigureScripts(const py::list& values) {
        std::vector<Runtime::ScriptBinding> bindings;
        try{for(const py::handle itemHandle:values){const auto item=py::reinterpret_borrow<py::dict>(itemHandle);Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(py::str(item["module"]).cast<std::string>());binding.Entity=py::str(item["entity"]).cast<std::string>();binding.TypeName=py::str(item["type"]).cast<std::string>();const auto properties=py::reinterpret_borrow<py::dict>(item["properties"]);for(const auto& pair:properties){const std::string name=py::str(pair.first).cast<std::string>();const py::handle value=pair.second;std::string text;if(py::isinstance<py::bool_>(value))text=value.cast<bool>()?"true":"false";else if(py::isinstance<py::sequence>(value)&&!py::isinstance<py::str>(value)){const auto sequence=py::reinterpret_borrow<py::sequence>(value);for(const auto part:sequence){if(!text.empty())text+=',';text+=py::str(part).cast<std::string>();}}else text=py::str(value).cast<std::string>();binding.Properties.emplace(name,std::move(text));}bindings.push_back(std::move(binding));}}catch(const py::error_already_set& error){m_bridgeError=error.what();return false;}
        m_bridgeError.clear();return m_engine->ConfigureScripts(std::move(bindings));
    }

private:
    struct LoadedScene { std::filesystem::path Path; std::unique_ptr<Scene> Data;
#ifdef _WIN32
        HANDLE Lock=INVALID_HANDLE_VALUE;
#else
        int Lock=-1;
#endif
    };
    Scene& SceneFor(LoadedScene& loaded){return &loaded==&m_loadedScenes[m_activeScene]?m_engine->GetScene():*loaded.Data;}
    Scene& SceneAt(std::size_t index){return index==m_activeScene?m_engine->GetScene():*m_loadedScenes[index].Data;}
    std::optional<std::size_t> FindLoadedScene(const std::string& id){const auto requested=std::filesystem::u8path(id);for(std::size_t i=0;i<m_loadedScenes.size();++i)if(SceneAt(i).GetUUID().ToString()==id||(!m_loadedScenes[i].Path.empty()&&std::filesystem::weakly_canonical(m_loadedScenes[i].Path)==std::filesystem::weakly_canonical(requested)))return i;return std::nullopt;}
    void AcquireSceneLock(LoadedScene& loaded){if(loaded.Path.empty()||!std::filesystem::exists(loaded.Path))return;
#ifdef _WIN32
        if(loaded.Lock==INVALID_HANDLE_VALUE)loaded.Lock=CreateFileW(loaded.Path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
#else
        if(loaded.Lock<0){loaded.Lock=open(loaded.Path.c_str(),O_RDONLY);if(loaded.Lock>=0&&flock(loaded.Lock,LOCK_EX|LOCK_NB)!=0){close(loaded.Lock);loaded.Lock=-1;}}
#endif
    }
    void ReleaseSceneLock(LoadedScene& loaded){
#ifdef _WIN32
        if(loaded.Lock!=INVALID_HANDLE_VALUE){CloseHandle(loaded.Lock);loaded.Lock=INVALID_HANDLE_VALUE;}
#else
        if(loaded.Lock>=0){flock(loaded.Lock,LOCK_UN);close(loaded.Lock);loaded.Lock=-1;}
#endif
    }
    void ClearLoadedScenes(){for(auto& loaded:m_loadedScenes)ReleaseSceneLock(loaded);m_loadedScenes.clear();}
    void UpdateStartupScene(){if(m_projectPath.empty()||m_scenePath.empty())return;std::error_code error;auto relative=std::filesystem::relative(m_scenePath,m_projectPath.parent_path(),error);if(error||relative.empty()||relative.is_absolute()||*relative.begin()=="..")return;m_engine->GetProject().StartupScene=relative;m_engine->SaveProject(m_projectPath);}
    std::pair<Scene*,Entity> FindEntity(const std::string& id){const UUID uuid=ParseUuid(id);for(std::size_t i=0;i<m_loadedScenes.size();++i){Scene& scene=SceneAt(i);Entity entity=scene.GetEntity(uuid);if(entity)return {&scene,entity};}return {nullptr,{}};}
    Entity RequireEntity(const std::string& id) {
        Entity entity = FindEntity(id).second;
        if (!entity) throw std::invalid_argument("Entity does not exist");
        return entity;
    }
    void CleanupSnapshot() {
        if (m_snapshot.empty()) return;
        std::error_code error; std::filesystem::remove(m_snapshot, error); m_snapshot.clear();
    }

    std::unique_ptr<Runtime::Engine> m_engine;
    std::filesystem::path m_projectPath;
    std::filesystem::path m_snapshot;
    std::filesystem::path m_scenePath;
    std::string m_bridgeError;
    std::vector<LoadedScene> m_loadedScenes;
    std::size_t m_activeScene = 0;
    bool m_playing = false;
    bool m_paused = false;
};

} // namespace
} // namespace Bazzalt::EditorBridge

PYBIND11_MODULE(_bazzalt_runtime, module) {
    module.doc() = "Private BAZZALT editor-to-runtime bridge";
    py::class_<Bazzalt::EditorBridge::EditorHost>(module, "EditorHost")
        .def(py::init<>()).def("load_project", &Bazzalt::EditorBridge::EditorHost::LoadProject)
        .def("load_scene", &Bazzalt::EditorBridge::EditorHost::LoadScene)
        .def("load_scene_additive", &Bazzalt::EditorBridge::EditorHost::LoadSceneAdditive)
        .def("activate_scene", &Bazzalt::EditorBridge::EditorHost::ActivateScene)
        .def("unload_scene", &Bazzalt::EditorBridge::EditorHost::UnloadScene)
        .def("rename_loaded_scene", &Bazzalt::EditorBridge::EditorHost::RenameLoadedScene)
        .def("is_scene_loaded", &Bazzalt::EditorBridge::EditorHost::IsSceneLoaded)
        .def("save_scene", &Bazzalt::EditorBridge::EditorHost::SaveScene)
        .def("new_scene", &Bazzalt::EditorBridge::EditorHost::NewScene)
        .def("last_error", &Bazzalt::EditorBridge::EditorHost::LastError)
        .def("project_directory", &Bazzalt::EditorBridge::EditorHost::ProjectDirectory)
        .def("asset_directory", &Bazzalt::EditorBridge::EditorHost::AssetDirectory)
        .def("entities", &Bazzalt::EditorBridge::EditorHost::Entities)
        .def("loaded_scenes", &Bazzalt::EditorBridge::EditorHost::LoadedScenes)
        .def("entity_details", &Bazzalt::EditorBridge::EditorHost::EntityDetails)
        .def("has_active_camera", &Bazzalt::EditorBridge::EditorHost::HasActiveCamera)
        .def("create_entity", &Bazzalt::EditorBridge::EditorHost::CreateEntity,
             py::arg("name"), py::arg("parent") = "")
        .def("destroy_entity", &Bazzalt::EditorBridge::EditorHost::DestroyEntity)
        .def("set_parent", &Bazzalt::EditorBridge::EditorHost::SetParent)
        .def("rename", &Bazzalt::EditorBridge::EditorHost::Rename)
        .def("set_transform", &Bazzalt::EditorBridge::EditorHost::SetTransform)
        .def("translate", &Bazzalt::EditorBridge::EditorHost::Translate)
        .def("instantiate_model", &Bazzalt::EditorBridge::EditorHost::InstantiateModel,
             py::arg("asset"), py::arg("parent") = "")
        .def("instantiate_model_path", &Bazzalt::EditorBridge::EditorHost::InstantiateModelPath,
             py::arg("path"), py::arg("parent") = "")
        .def("add_component", &Bazzalt::EditorBridge::EditorHost::AddComponent)
        .def("remove_component", &Bazzalt::EditorBridge::EditorHost::RemoveComponent)
        .def("set_component_enabled", &Bazzalt::EditorBridge::EditorHost::SetComponentEnabled)
        .def("component_types", &Bazzalt::EditorBridge::EditorHost::ComponentTypes)
        .def("scene_info", &Bazzalt::EditorBridge::EditorHost::SceneInfo)
        .def("capture_scene", &Bazzalt::EditorBridge::EditorHost::CaptureScene)
        .def("restore_scene", &Bazzalt::EditorBridge::EditorHost::RestoreScene)
        .def("create_viewport", &Bazzalt::EditorBridge::EditorHost::CreateViewport)
        .def("resize_viewport", &Bazzalt::EditorBridge::EditorHost::ResizeViewport)
        .def("destroy_viewport", &Bazzalt::EditorBridge::EditorHost::DestroyViewport)
        .def("set_scene_camera", &Bazzalt::EditorBridge::EditorHost::SetSceneCamera)
        .def("set_component_property", &Bazzalt::EditorBridge::EditorHost::SetComponentProperty)
        .def("set_gizmo", &Bazzalt::EditorBridge::EditorHost::SetGizmo)
        .def("set_gizmo_position", &Bazzalt::EditorBridge::EditorHost::SetGizmoPosition)
        .def("set_gizmo_hover", &Bazzalt::EditorBridge::EditorHost::SetGizmoHover)
        .def("set_grid", &Bazzalt::EditorBridge::EditorHost::SetGrid)
        .def("configure_scripts", &Bazzalt::EditorBridge::EditorHost::ConfigureScripts)
        .def("play", &Bazzalt::EditorBridge::EditorHost::Play)
        .def("pause", &Bazzalt::EditorBridge::EditorHost::Pause)
        .def("step", &Bazzalt::EditorBridge::EditorHost::Step)
        .def("tick", &Bazzalt::EditorBridge::EditorHost::Tick)
        .def("stop", &Bazzalt::EditorBridge::EditorHost::Stop)
        .def("is_playing", &Bazzalt::EditorBridge::EditorHost::IsPlaying)
        .def("is_paused", &Bazzalt::EditorBridge::EditorHost::IsPaused);
}
