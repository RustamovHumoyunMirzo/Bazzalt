#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <charconv>
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
#include "Runtime/TextureLibrary.h"
#include "Bazzalt/Material.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include "Runtime/InputAccess.h"
#include "EditorBridge/GuiBridge.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Rendering/LightParameters.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
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
    if (entity.HasComponent<PrimitiveObject>()) components.append("Primitive Object");
    if (entity.HasComponent<ModelInstance>()) components.append("Model Instance");
    if (entity.HasComponent<ModelNode>()) components.append("Model Node");
    if (entity.HasComponent<GaussianBlur>()) components.append("Gaussian Blur");
    if (entity.HasComponent<Vignette>()) components.append("Vignette");
    if (entity.HasComponent<SceneQueryBounds>()) components.append("Scene Query Bounds");
    py::dict enabled;
    if (const auto* value=entity.TryGetComponent<Camera>()) enabled["Camera"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Light>()) enabled["Light"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Mesh>()) enabled["Mesh"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<PrimitiveObject>()) enabled["Primitive Object"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<ModelInstance>()) enabled["Model Instance"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<ModelNode>()) enabled["Model Node"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<GaussianBlur>()) enabled["Gaussian Blur"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<Vignette>()) enabled["Vignette"]=value->IsEnabled();
    if (const auto* value=entity.TryGetComponent<SceneQueryBounds>()) enabled["Scene Query Bounds"]=value->IsEnabled();
    py::dict data;
    if (const auto* camera=entity.TryGetComponent<Camera>()) {
        py::dict v;v["Render Target"]=camera->RenderTarget.ToString();v["Projection"]=static_cast<int>(camera->Projection);v["Field of View"]=ToDegrees(camera->VerticalFieldOfView);v["Orthographic Size"]=camera->OrthographicSize;v["Near"]=camera->NearPlane;v["Far"]=camera->FarPlane;v["Aspect Ratio"]=camera->AspectRatio;v["Aspect Mode"]=static_cast<int>(camera->AspectMode);v["Viewport"]=py::make_tuple(camera->Viewport.X,camera->Viewport.Y,camera->Viewport.Width,camera->Viewport.Height);v["Priority"]=camera->Priority;v["Active"]=camera->Active;v["Clear Color"]=py::make_tuple(camera->ClearColor.X,camera->ClearColor.Y,camera->ClearColor.Z,camera->ClearColor.W);v["Post Processing"]=camera->PostProcessing.Enabled;v["Bloom"]=camera->PostProcessing.Bloom;v["Ambient Occlusion"]=camera->PostProcessing.AmbientOcclusion;v["Anti Aliasing"]=static_cast<int>(camera->PostProcessing.AntiAliasingMode);v["Tone Mapping"]=static_cast<int>(camera->PostProcessing.ToneMappingMode);v["Exposure"]=camera->PostProcessing.Exposure;v["Depth of Field"]=camera->PostProcessing.DepthOfField.Enabled;v["Focus Distance"]=camera->PostProcessing.DepthOfField.FocusDistance;v["Aperture"]=camera->PostProcessing.DepthOfField.Aperture;v["Shutter Speed"]=camera->PostProcessing.DepthOfField.ShutterSpeed;v["Sensitivity"]=camera->PostProcessing.DepthOfField.Sensitivity;data["Camera"]=v;
    }
    if (const auto* light=entity.TryGetComponent<Light>()) {
        const auto effective=Runtime::SanitizeLight(*light);py::dict v;
        v["Type"]=static_cast<int>(effective.Type);v["Color"]=py::make_tuple(effective.Color.X,effective.Color.Y,effective.Color.Z);v["Intensity"]=effective.Intensity;
        if(effective.Type==LightType::Point||effective.Type==LightType::Spot)v["Range"]=effective.Range;
        if(effective.Type==LightType::Spot){v["Inner Cone"]=ToDegrees(effective.InnerConeAngle);v["Outer Cone"]=ToDegrees(effective.OuterConeAngle);}
        if(effective.Type==LightType::Sun){v["Sun Angular Radius"]=ToDegrees(effective.SunAngularRadius);v["Sun Halo Size"]=effective.SunHaloSize;v["Sun Halo Falloff"]=effective.SunHaloFalloff;}
        v["Cast Shadows"]=effective.CastShadows;data["Light"]=v;
    }
    if (const auto* mesh=entity.TryGetComponent<Mesh>()) { py::dict v;v["Mesh Asset"]=mesh->MeshAsset.ToString();v["Model Node Index"]=mesh->ModelNodeIndex;v["Material Asset"]=mesh->MaterialAsset.ToString();py::list slots;for(UUID id:mesh->Materials)slots.append(id.ToString());v["Material Slots"]=slots;v["Material Count"]=static_cast<int>(mesh->Materials.size());v["Layer Mask"]=mesh->LayerMask;v["Visible"]=mesh->Visible;v["Cast Shadows"]=mesh->CastShadows;v["Receive Shadows"]=mesh->ReceiveShadows;data["Mesh"]=v; }
    if(const auto* primitive=entity.TryGetComponent<PrimitiveObject>()){py::dict v;v["Shape"]=static_cast<int>(primitive->Shape);switch(primitive->Shape){case PrimitiveShape::Cube:v["Size"]=py::make_tuple(primitive->Size.X,primitive->Size.Y,primitive->Size.Z);break;case PrimitiveShape::Sphere:v["Radius"]=primitive->Radius;v["Segments"]=primitive->Segments;v["Rings"]=primitive->Rings;break;case PrimitiveShape::Cylinder:v["Radius"]=primitive->Radius;v["Height"]=primitive->Height;v["Segments"]=primitive->Segments;break;case PrimitiveShape::Capsule:v["Radius"]=primitive->Radius;v["Height"]=primitive->Height;v["Segments"]=primitive->Segments;v["Rings"]=primitive->Rings;break;case PrimitiveShape::Plane:v["Width"]=primitive->Width;v["Depth"]=primitive->Depth;break;case PrimitiveShape::Cone:v["Radius"]=primitive->Radius;v["Height"]=primitive->Height;v["Segments"]=primitive->Segments;break;case PrimitiveShape::Torus:v["Major Radius"]=primitive->MajorRadius;v["Minor Radius"]=primitive->MinorRadius;v["Segments"]=primitive->Segments;v["Rings"]=primitive->Rings;break;}v["Color"]=py::make_tuple(primitive->Color.X,primitive->Color.Y,primitive->Color.Z,primitive->Color.W);v["Material Asset"]=primitive->MaterialAsset.ToString();v["Layer Mask"]=primitive->LayerMask;v["Visible"]=primitive->Visible;v["Cast Shadows"]=primitive->CastShadows;v["Receive Shadows"]=primitive->ReceiveShadows;data["Primitive Object"]=v;}
    if(const auto* mesh=entity.TryGetComponent<Mesh>())for(std::size_t index=0;index<mesh->Materials.size();++index)
        data["Mesh"].cast<py::dict>()[py::str("Material Slot "+std::to_string(index)+" Asset")]=mesh->Materials[index].ToString();
    if (const auto* model=entity.TryGetComponent<ModelInstance>()){py::dict v;v["Model Asset"]=model->ModelAsset.ToString();data["Model Instance"]=v;}
    if (const auto* node=entity.TryGetComponent<ModelNode>()){py::dict v;v["Model Asset"]=node->ModelAsset.ToString();v["Source Index"]=node->SourceIndex;v["Mesh Index"]=node->MeshIndex;v["Stable Path"]=node->StablePath;v["Has Mesh"]=node->HasMesh;data["Model Node"]=v;}
    if (const auto* blur=entity.TryGetComponent<GaussianBlur>()) { py::dict v;v["Size"]=blur->Size;data["Gaussian Blur"]=v; }
    if (const auto* vignette=entity.TryGetComponent<Vignette>()) { py::dict v;v["Color"]=py::make_tuple(vignette->Color.X,vignette->Color.Y,vignette->Color.Z,vignette->Color.W);v["Intensity"]=vignette->Intensity;v["Smoothness"]=vignette->Smoothness;v["Roundness"]=vignette->Roundness;data["Vignette"]=v; }
    if (const auto* bounds=entity.TryGetComponent<SceneQueryBounds>()) { py::dict v;v["Shape"]=static_cast<int>(bounds->Shape);v["Center"]=py::make_tuple(bounds->Center.X,bounds->Center.Y,bounds->Center.Z);v["Extents"]=py::make_tuple(bounds->Extents.X,bounds->Extents.Y,bounds->Extents.Z);v["Radius"]=bounds->Radius;v["Layer Mask"]=bounds->LayerMask;data["Scene Query Bounds"]=v; }
    InspectGUI(entity,components,enabled,data);
    result["components"]=components;result["component_enabled"]=enabled;
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
    bool RenameAssetDirectory(const std::string& source,const std::string& destination){
        if(m_projectPath.empty())return false;
        const auto root=std::filesystem::weakly_canonical(m_projectPath.parent_path()/m_engine->GetProject().AssetDirectory);
        const auto old=std::filesystem::weakly_canonical(std::filesystem::u8path(source));
        const auto target=std::filesystem::u8path(destination).lexically_normal();const auto relative=old.lexically_relative(root);
        if(relative.empty()||relative=="."||*relative.begin()==".."||old.parent_path()!=target.parent_path()||!std::filesystem::is_directory(old)||std::filesystem::exists(target))return false;
        std::vector<std::pair<std::size_t,std::filesystem::path>> moved;
        for(std::size_t i=0;i<m_loadedScenes.size();++i){
            const auto rel=m_loadedScenes[i].Path.lexically_relative(old);
            if(!rel.empty()&&*rel.begin()!=".."){moved.emplace_back(i,target/rel);ReleaseSceneLock(m_loadedScenes[i]);}
        }
        std::error_code error;std::filesystem::rename(old,target,error);
        if(!error)for(const auto& [index,path]:moved){m_loadedScenes[index].Path=path;if(index==m_activeScene){m_scenePath=path;UpdateStartupScene();}}
        for(const auto& [index,_]:moved)AcquireSceneLock(m_loadedScenes[index]);
        if(error){m_bridgeError=error.message();return false;}
        m_bridgeError.clear();return true;
    }
    std::string LastError() const { return m_bridgeError.empty()?m_engine->GetLastError():m_bridgeError; }
    std::vector<std::string> SupportedRenderingBackends() const {return Runtime::Engine::SupportedRenderingBackends();}
    bool ConfigureRenderingBackend(const std::string& backend){return m_engine->ConfigureRenderingBackend(backend);}
    py::str ProjectDirectory() const { return m_projectPath.empty() ? py::str() : PathText(m_projectPath.parent_path()); }
    py::dict ProjectInfo() const {
        py::dict result;if(m_projectPath.empty())return result;
        const auto& project=m_engine->GetProject();
        result["name"]=project.Name;result["uuid"]=project.ProjectUUID.ToString();
        result["asset_directory"]=PathText(project.AssetDirectory);
        result["startup_scene"]=PathText(project.StartupScene);
        result["project_path"]=PathText(m_projectPath);result["properties"]=py::cast(project.Properties);
        return result;
    }
    bool UpdateProjectInfo(const std::string& name,const std::map<std::string,std::string>& properties) {
        m_bridgeError.clear();
        if(m_projectPath.empty()||name.find_first_not_of(" \t\r\n")==std::string::npos){m_bridgeError="Project name must not be empty.";return false;}
        auto& project=m_engine->GetProject();const auto previous=project;
        project.Name=name;project.Properties=properties;
        if(!m_engine->SaveProject(m_projectPath)){project=previous;return false;}
        return true;
    }
    bool RefreshAssets() { return m_engine && m_engine->RefreshAssets(); }
    py::dict TextureAssetInfo(const std::string& source){
        TextureDescriptor d;std::string error;py::dict result;if(!Runtime::ReadTextureDescriptor(std::filesystem::u8path(source),d,error)){m_bridgeError=error;return result;}
        result["Width"]=d.Width;result["Height"]=d.Height;result["Samples"]=d.Samples;result["Mipmaps"]=d.Mipmaps;result["Mip Levels"]=d.MipLevels;
        result["Color Format"]=int(d.ColorFormat);result["Depth Format"]=int(d.DepthFormat);result["Filter"]=int(d.Filter);result["Wrap"]=int(d.Wrap);result["Clear Color"]=py::make_tuple(d.ClearColor.X,d.ClearColor.Y,d.ClearColor.Z,d.ClearColor.W);return result;
    }
    bool SaveTextureAsset(const std::string& source,const py::dict& values){
        if(m_projectPath.empty())return false;const auto path=std::filesystem::weakly_canonical(std::filesystem::u8path(source));const auto root=std::filesystem::weakly_canonical(m_projectPath.parent_path()/m_engine->GetProject().AssetDirectory);const auto relative=path.lexically_relative(root);
        if(relative.empty()||*relative.begin()==".."||[&]{auto extension=path.extension().u8string();std::transform(extension.begin(),extension.end(),extension.begin(),[](char8_t c){return c>=u8'A'&&c<=u8'Z'?char8_t(c+32):c;});return extension!=u8".btexture";}()){m_bridgeError="Texture must be inside the project Assets directory.";return false;}
        TextureDescriptor d;std::string error;if(!Runtime::ReadTextureDescriptor(path,d,error)){m_bridgeError=error;return false;}
        d.Width=values["Width"].cast<std::uint32_t>();d.Height=values["Height"].cast<std::uint32_t>();d.Samples=values["Samples"].cast<std::uint8_t>();d.Mipmaps=values["Mipmaps"].cast<bool>();d.MipLevels=values["Mip Levels"].cast<std::uint8_t>();
        d.ColorFormat=static_cast<TextureColorFormat>(values["Color Format"].cast<int>());d.DepthFormat=static_cast<TextureDepthFormat>(values["Depth Format"].cast<int>());d.Filter=static_cast<TextureFilter>(values["Filter"].cast<int>());d.Wrap=static_cast<TextureWrap>(values["Wrap"].cast<int>());
        const auto color=values["Clear Color"].cast<std::array<float,4>>();d.ClearColor={color[0],color[1],color[2],color[3]};if(!d.Mipmaps)d.MipLevels=0;
        else {unsigned levels=1,size=std::max(d.Width,d.Height);while(size>1){++levels;size>>=1;}d.MipLevels=static_cast<std::uint8_t>(std::min<unsigned>(levels,d.MipLevels));}
        if(!Runtime::SaveTextureDescriptor(path,d,error)){m_bridgeError=error;return false;}return m_engine->RefreshTextureAsset(path);
    }
    py::list UsedShaderAssets() {
        py::list result;
        for(std::size_t i=0;i<m_loadedScenes.size();++i){auto view=SceneAt(i).GetRegistry().view<Camera>();for(auto handle:view){const auto& camera=view.get<Camera>(handle);if(!camera.IsEnabled())continue;for(const auto& effect:camera.PostProcessing.CustomEffects.GetEffects())if(effect.Enabled&&effect.ShaderAsset)result.append(effect.ShaderAsset.ToString());}}
        return result;
    }
    py::dict AssetInfo(const std::string& text) const {
        py::dict result;UUID id;
        const auto asset=UUID::TryParse(text,id)?AssetManager::GetAsset(id):AssetManager::GetAsset(std::filesystem::u8path(text));
        if(asset){result["uuid"]=asset->Id.ToString();result["path"]=PathText(asset->SourcePath);result["cache"]=PathText(asset->CachePath);result["importer"]=asset->Importer;result["error"]=asset->LastError;result["settings"]=m_engine->GetAssetImportSettings(asset->Id);}
        return result;
    }
    py::str AssetDirectory() const {
        if (m_projectPath.empty()) return py::str();
        return PathText(m_projectPath.parent_path() / m_engine->GetProject().AssetDirectory);
    }
    bool SetEnvironmentImportSettings(const std::string& id,const PropertyMap& settings){return m_engine->SetEnvironmentImportSettings(ParseUuid(id),settings);}

    py::list SceneEntities(Scene& scene) {
        py::list result;
        const auto meshBounds=(&scene==&m_engine->GetScene())?m_engine->GetEditorMeshBounds():std::unordered_map<UUID,std::pair<Vec3,Vec3>>{};
        std::function<void(Entity)> append = [&](Entity parent) {
            for (Entity child : parent.GetChildren()) {
                auto snapshot=SnapshotEntity(scene,child);
                if(auto found=meshBounds.find(child.GetUUID());found!=meshBounds.end()){
                    const auto& [minimum,maximum]=found->second;
                    snapshot["mesh_bounds"]=py::make_tuple(py::make_tuple(minimum.X,minimum.Y,minimum.Z),py::make_tuple(maximum.X,maximum.Y,maximum.Z));
                }
                result.append(snapshot);
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
    py::dict EntityPose(const std::string& id) {
        auto [scene,entity]=FindEntity(id);if(!entity)return {};
        const auto position=entity.GetWorldMatrix().TransformPoint({});py::dict result;
        result["world_position"]=py::make_tuple(position.X,position.Y,position.Z);
        result["position"]=result["world_position"];result["scene_active"]=(scene==&m_engine->GetScene());
        const auto& local=entity.GetComponent<Transform>();
        result["local_position"]=py::make_tuple(local.Position.X,local.Position.Y,local.Position.Z);
        result["local_rotation"]=py::make_tuple(local.Rotation.X,local.Rotation.Y,local.Rotation.Z,local.Rotation.W);
        result["local_scale"]=py::make_tuple(local.Scale.X,local.Scale.Y,local.Scale.Z);return result;
    }
    bool RestoreTransforms(const py::dict& poses) {
        std::vector<std::pair<Entity,Transform>> changes;
        for(const auto& item:poses){auto [scene,entity]=FindEntity(py::cast<std::string>(item.first));if(!entity)return false;
            const auto data=py::cast<py::dict>(item.second);const auto p=data["local_position"].cast<std::array<float,3>>();const auto r=data["local_rotation"].cast<std::array<float,4>>();const auto s=data["local_scale"].cast<std::array<float,3>>();
            Transform value;value.Position={p[0],p[1],p[2]};value.Rotation={r[0],r[1],r[2],r[3]};value.Scale={s[0],s[1],s[2]};changes.emplace_back(entity,value);
        }
        for(auto& [entity,value]:changes)entity.GetComponent<Transform>()=value;
        return true;
    }
    std::size_t EntityCount() const {return m_engine->GetScene().GetEntityCount();}
    auto Statistics() const {return m_engine->GetEditorStatistics();}
    void SetStatisticsText(std::string text,bool visible){m_engine->SetEditorStatisticsText(std::move(text),visible);}
    bool HasActiveCamera() const {
        const auto cameras = m_engine->GetScene().GetRegistry().view<Camera>();
        for (const auto handle : cameras)
            if (const auto& camera=cameras.get<Camera>(handle);
                camera.Active && camera.IsEnabled()) return true;
        return false;
    }
    bool HasGameOutput() const {
        const auto& registry=m_engine->GetScene().GetRegistry();
        for(auto handle:registry.view<Frame>()){const auto& frame=registry.get<Frame>(handle);if(frame.Enabled&&frame.Visible&&frame.Mode==FrameMode::Viewport)return true;}
        for(auto handle:registry.view<Camera>()){const auto& camera=registry.get<Camera>(handle);const auto* target=registry.try_get<CameraRenderTarget>(handle);if(camera.Enabled&&camera.Active&&!camera.RenderTarget&&(!target||!target->Enabled))return true;}
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
        else if (type == "Primitive Object") { if (!entity.HasComponent<PrimitiveObject>()) entity.AddComponent<PrimitiveObject>(); }
        else if (type == "Gaussian Blur") { if (!entity.HasComponent<GaussianBlur>()) entity.AddComponent<GaussianBlur>(); }
        else if (type == "Vignette") { if (!entity.HasComponent<Vignette>()) entity.AddComponent<Vignette>(); }
        else if (type == "Scene Query Bounds") { if (!entity.HasComponent<SceneQueryBounds>()) entity.AddComponent<SceneQueryBounds>(); }
        else return GuiDispatch(entity,type,[]<class T>(Entity e){if(!e.HasComponent<T>())e.AddComponent<T>();});
        return true;
    }
    bool RemoveComponent(const std::string& id, const std::string& type) {
        Entity entity = RequireEntity(id);
        if (type == "Camera" && entity.HasComponent<Camera>()) entity.RemoveComponent<Camera>();
        else if (type == "Light" && entity.HasComponent<Light>()) entity.RemoveComponent<Light>();
        else if (type == "Mesh" && entity.HasComponent<Mesh>()) entity.RemoveComponent<Mesh>();
        else if (type == "Primitive Object" && entity.HasComponent<PrimitiveObject>()) entity.RemoveComponent<PrimitiveObject>();
        else if (type == "Gaussian Blur" && entity.HasComponent<GaussianBlur>()) entity.RemoveComponent<GaussianBlur>();
        else if (type == "Vignette" && entity.HasComponent<Vignette>()) entity.RemoveComponent<Vignette>();
        else if (type == "Scene Query Bounds" && entity.HasComponent<SceneQueryBounds>()) entity.RemoveComponent<SceneQueryBounds>();
        else return GuiDispatch(entity,type,[]<class T>(Entity e){e.RemoveComponent<T>();});
        return true;
    }
    bool SetComponentEnabled(const std::string& id, const std::string& type, bool enabled) {
        Entity entity = RequireEntity(id);
        if (type == "Camera" && entity.HasComponent<Camera>()) entity.SetComponentEnabled<Camera>(enabled);
        else if (type == "Light" && entity.HasComponent<Light>()) entity.SetComponentEnabled<Light>(enabled);
        else if (type == "Mesh" && entity.HasComponent<Mesh>()) entity.SetComponentEnabled<Mesh>(enabled);
        else if (type == "Primitive Object" && entity.HasComponent<PrimitiveObject>()) entity.SetComponentEnabled<PrimitiveObject>(enabled);
        else if (type == "Model Instance" && entity.HasComponent<ModelInstance>()) entity.SetComponentEnabled<ModelInstance>(enabled);
        else if (type == "Model Node" && entity.HasComponent<ModelNode>()) entity.SetComponentEnabled<ModelNode>(enabled);
        else if (type == "Gaussian Blur" && entity.HasComponent<GaussianBlur>()) entity.SetComponentEnabled<GaussianBlur>(enabled);
        else if (type == "Vignette" && entity.HasComponent<Vignette>()) entity.SetComponentEnabled<Vignette>(enabled);
        else if (type == "Scene Query Bounds" && entity.HasComponent<SceneQueryBounds>()) entity.SetComponentEnabled<SceneQueryBounds>(enabled);
        else return GuiDispatch(entity,type,[enabled]<class T>(Entity e){if(e.HasComponent<T>())e.SetComponentEnabled<T>(enabled);});
        return true;
    }
    py::list ComponentTypes() const {
        py::list result;
        for (const char* name : {"Camera", "Light", "Mesh", "Primitive Object", "Scene Query Bounds", "Gaussian Blur", "Vignette"})
            result.append(name);
        for(auto name:GuiNames)result.append(name);
        return result;
    }
    py::dict SceneInfo() const {
        py::dict result;
        result["uuid"] = m_engine->GetScene().GetUUID().ToString();
        result["name"] = m_scenePath.empty() ? py::str("Untitled") : PathText(m_scenePath.stem());
        result["path"] = m_scenePath.empty() ? py::str() : PathText(m_scenePath);
        return result;
    }
    py::dict SceneEnvironmentInfo(const std::string& id){
        const auto index=FindLoadedScene(id);if(!index)return {};
        const auto& value=SceneAt(*index).GetEnvironment();py::dict result;
        UUID resolved=value.Mode==SceneEnvironmentMode::Map?value.SourceAsset:UUID{};
        if(value.Mode==SceneEnvironmentMode::Material&&value.MaterialAsset){const auto material=Material::Load(value.MaterialAsset);for(const auto& parameter:material.GetParameters())if(parameter.Type==ShaderParameterType::Texture2D){const auto texture=material.GetTexture(parameter.Name);const auto asset=AssetManager::GetAsset(texture);if(asset&&(asset->SourcePath.extension()==".hdr"||asset->SourcePath.extension()==".exr"||asset->SourcePath.extension()==".ktx")){resolved=texture;break;}}}
        result["resolved_source"]=resolved.ToString();
        result["mode"]=static_cast<int>(value.Mode);result["source"]=value.SourceAsset.ToString();result["material"]=value.MaterialAsset.ToString();result["intensity"]=value.Intensity;result["rotation"]=py::make_tuple(value.Rotation.X,value.Rotation.Y,value.Rotation.Z);result["clear_color"]=py::make_tuple(value.ClearColor.X,value.ClearColor.Y,value.ClearColor.Z,value.ClearColor.W);result["ibl"]=value.ImageBasedLighting;result["skybox"]=value.SkyboxVisible;result["show_sun"]=value.ShowSun;result["lighting_ready"]=*index==m_activeScene&&m_engine->HasEnvironmentLighting();result["skybox_ready"]=*index==m_activeScene&&m_engine->HasEnvironmentSkybox();return result;
    }
    bool SetSceneEnvironment(const std::string& id,const py::dict& data){
        const auto index=FindLoadedScene(id);if(!index)return false;
        auto value=SceneAt(*index).GetEnvironment();
        if(data.contains("mode")){const auto mode=data["mode"].cast<int>();if(mode<0||mode>1)return false;value.Mode=static_cast<SceneEnvironmentMode>(mode);}
        if(data.contains("source"))value.SourceAsset=ParseUuid(data["source"].cast<std::string>());
        if(data.contains("material"))value.MaterialAsset=ParseUuid(data["material"].cast<std::string>());
        const auto validAsset=[](UUID uuid,bool material){if(!uuid)return true;const auto info=AssetManager::GetAsset(uuid);if(!info)return false;const auto ext=info->SourcePath.extension();return material?ext==".matinst":ext==".hdr"||ext==".exr"||ext==".ktx";};
        if(value.Mode==SceneEnvironmentMode::Map?!validAsset(value.SourceAsset,false):!validAsset(value.MaterialAsset,true))return false;
        if(data.contains("intensity"))value.Intensity=data["intensity"].cast<float>();
        if(data.contains("rotation")){const auto v=data["rotation"].cast<std::array<float,3>>();value.Rotation={v[0],v[1],v[2]};}
        if(data.contains("clear_color")){const auto v=data["clear_color"].cast<std::array<float,4>>();value.ClearColor={v[0],v[1],v[2],v[3]};}
        if(data.contains("ibl"))value.ImageBasedLighting=data["ibl"].cast<bool>();
        if(data.contains("skybox"))value.SkyboxVisible=data["skybox"].cast<bool>();
        if(data.contains("show_sun"))value.ShowSun=data["show_sun"].cast<bool>();
        return SceneAt(*index).SetEnvironment(value);
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
        if(GuiRegistry().Find("Bazzalt."+type))return SetGUIProperty(entity,type,property,value);
        if(type=="Camera"&&entity.HasComponent<Camera>()){auto&v=entity.GetComponent<Camera>();if(property=="Render Target"){UUID id;if(!UUID::TryParse(value.cast<std::string>(),id))return false;if(id&&!Texture::Load(id).IsRenderTarget())return false;v.RenderTarget=id;}else if(property=="Projection")v.Projection=static_cast<CameraProjection>(value.cast<int>());else if(property=="Field of View")v.VerticalFieldOfView=ToRadians(value.cast<float>());else if(property=="Orthographic Size")v.OrthographicSize=value.cast<float>();else if(property=="Near")v.NearPlane=value.cast<float>();else if(property=="Far")v.FarPlane=value.cast<float>();else if(property=="Aspect Ratio")v.AspectRatio=value.cast<float>();else if(property=="Aspect Mode")v.AspectMode=static_cast<CameraAspectMode>(value.cast<int>());else if(property=="Viewport"){auto a=value.cast<std::array<float,4>>();v.Viewport={a[0],a[1],a[2],a[3]};}else if(property=="Priority")v.Priority=value.cast<int>();else if(property=="Active")v.Active=value.cast<bool>();else if(property=="Clear Color"){auto a=value.cast<std::array<float,4>>();v.ClearColor={a[0],a[1],a[2],a[3]};}else if(property=="Post Processing")v.PostProcessing.Enabled=value.cast<bool>();else if(property=="Bloom")v.PostProcessing.Bloom=value.cast<bool>();else if(property=="Ambient Occlusion")v.PostProcessing.AmbientOcclusion=value.cast<bool>();else if(property=="Anti Aliasing")v.PostProcessing.AntiAliasingMode=static_cast<AntiAliasing>(value.cast<int>());else if(property=="Tone Mapping")v.PostProcessing.ToneMappingMode=static_cast<ToneMapping>(value.cast<int>());else if(property=="Exposure")v.PostProcessing.Exposure=value.cast<float>();else if(property=="Depth of Field")v.PostProcessing.DepthOfField.Enabled=value.cast<bool>();else if(property=="Focus Distance")v.PostProcessing.DepthOfField.FocusDistance=value.cast<float>();else if(property=="Aperture")v.PostProcessing.DepthOfField.Aperture=value.cast<float>();else if(property=="Shutter Speed")v.PostProcessing.DepthOfField.ShutterSpeed=value.cast<float>();else if(property=="Sensitivity")v.PostProcessing.DepthOfField.Sensitivity=value.cast<float>();else return false;return true;}
        if(type=="Light"&&entity.HasComponent<Light>()){
            auto& stored=entity.GetComponent<Light>();auto v=stored;
            if(property=="Type"){const int index=value.cast<int>();if(index<0||index>3)return false;v.Type=static_cast<LightType>(index);}
            else if(property=="Color"){auto a=value.cast<std::array<float,3>>();for(float c:a)if(!std::isfinite(c)||c<0)return false;v.Color={a[0],a[1],a[2]};}
            else if(property=="Cast Shadows")v.CastShadows=value.cast<bool>();
            else if(property=="Enabled")v.Enabled=value.cast<bool>();
            else{
                const float number=value.cast<float>();if(!std::isfinite(number)||number<0)return false;
                if(property=="Intensity")v.Intensity=number;
                else if(property=="Range")v.Range=number;
                else if(property=="Inner Cone"){v.InnerConeAngle=ToRadians(number);v.OuterConeAngle=std::max(v.OuterConeAngle,v.InnerConeAngle);}
                else if(property=="Outer Cone"){v.OuterConeAngle=ToRadians(number);v.InnerConeAngle=std::min(v.InnerConeAngle,v.OuterConeAngle);}
                else if(property=="Sun Angular Radius")v.SunAngularRadius=ToRadians(number);
                else if(property=="Sun Halo Size")v.SunHaloSize=number;
                else if(property=="Sun Halo Falloff")v.SunHaloFalloff=number;
                else return false;
            }
            stored=Runtime::SanitizeLight(v);return true;
        }
        if(type=="Mesh"&&entity.HasComponent<Mesh>()&&property.starts_with("Material Slot ")&&property.ends_with(" Asset")){
            auto& mesh=entity.GetComponent<Mesh>();const auto indexText=std::string_view(property).substr(14,property.size()-20);std::size_t index=0;
            const auto parsed=std::from_chars(indexText.data(),indexText.data()+indexText.size(),index);
            if(parsed.ec!=std::errc{}||parsed.ptr!=indexText.data()+indexText.size()||index>=mesh.Materials.size())return false;
            const auto text=value.cast<std::string>();UUID id;
            if(text!="0"&&!UUID::TryParse(text,id))return false;
            if(id){const auto asset=AssetManager::GetAsset(id);if(!asset||asset->SourcePath.extension()!=".matinst")return false;}
            mesh.Materials[index]=id;return true;
        }
        if(type=="Mesh"&&entity.HasComponent<Mesh>()){auto&v=entity.GetComponent<Mesh>();if((property=="Mesh Asset"||property=="Material Asset")){const auto text=value.cast<std::string>();UUID id;if(text!="0"&&!UUID::TryParse(text,id)){const auto asset=AssetManager::GetAsset(std::filesystem::u8path(text));if(!asset)return false;id=asset->Id;}if(property=="Mesh Asset")v.MeshAsset=id;else {if(id){const auto asset=AssetManager::GetAsset(id);if(!asset||asset->SourcePath.extension()!=".matinst")return false;}v.MaterialAsset=id;}}else if(property=="Model Node Index")v.ModelNodeIndex=value.cast<std::uint32_t>();else if(property=="Visible")v.Visible=value.cast<bool>();else if(property=="Cast Shadows")v.CastShadows=value.cast<bool>();else if(property=="Receive Shadows")v.ReceiveShadows=value.cast<bool>();else if(property=="Layer Mask")v.LayerMask=static_cast<std::uint8_t>(value.cast<int>());else return false;return true;}
        if(type=="Primitive Object"&&entity.HasComponent<PrimitiveObject>()){auto&v=entity.GetComponent<PrimitiveObject>();if(property=="Material Asset"){const auto text=value.cast<std::string>();UUID id;if(text!="0"&&!UUID::TryParse(text,id))return false;if(id){const auto asset=AssetManager::GetAsset(id);if(!asset||asset->SourcePath.extension()!=".matinst")return false;}v.MaterialAsset=id;}else if(property=="Shape")v.Shape=static_cast<PrimitiveShape>(value.cast<int>());else if(property=="Size"){auto a=value.cast<std::array<float,3>>();v.Size={a[0],a[1],a[2]};}else if(property=="Radius")v.Radius=std::max(.001f,value.cast<float>());else if(property=="Height")v.Height=std::max(.001f,value.cast<float>());else if(property=="Width")v.Width=std::max(.001f,value.cast<float>());else if(property=="Depth")v.Depth=std::max(.001f,value.cast<float>());else if(property=="Major Radius")v.MajorRadius=std::max(.001f,value.cast<float>());else if(property=="Minor Radius")v.MinorRadius=std::clamp(value.cast<float>(),.001f,v.MajorRadius);else if(property=="Segments")v.Segments=std::clamp(value.cast<std::uint32_t>(),3u,128u);else if(property=="Rings")v.Rings=std::clamp(value.cast<std::uint32_t>(),2u,128u);else if(property=="Color"){auto a=value.cast<std::array<float,4>>();v.Color={a[0],a[1],a[2],a[3]};}else if(property=="Layer Mask")v.LayerMask=static_cast<std::uint8_t>(value.cast<int>());else if(property=="Visible")v.Visible=value.cast<bool>();else if(property=="Cast Shadows")v.CastShadows=value.cast<bool>();else if(property=="Receive Shadows")v.ReceiveShadows=value.cast<bool>();else return false;return true;}
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
    std::string PickPrimitive(const std::array<float,3>& origin,const std::array<float,3>& direction){auto id=m_engine->PickEditorPrimitive({origin[0],origin[1],origin[2]},{direction[0],direction[1],direction[2]});return id.IsRoot()?std::string{}:id.ToString();}
    py::object RaycastEditor(const std::array<float,3>& origin,const std::array<float,3>& direction,const std::vector<std::string>& excluded){std::unordered_set<UUID> ignored;for(const auto& value:excluded)if(!value.empty())ignored.insert(ParseUuid(value));Vec3 position;const auto id=m_engine->PickEditorPrimitive({origin[0],origin[1],origin[2]},{direction[0],direction[1],direction[2]},&position,ignored);if(id.IsRoot())return py::none();return py::make_tuple(position.X,position.Y,position.Z);}
    void SetObjectHover(const std::string& id,const std::array<float,3>& eye){m_engine->SetEditorObjectHover(id.empty()?UUID{}:ParseUuid(id),{eye[0],eye[1],eye[2]});}
    void SetSelectionOutline(const std::vector<std::string>& ids){std::vector<UUID> selected;for(const auto& id:ids)if(!id.empty())selected.push_back(ParseUuid(id));m_engine->SetEditorSelection(std::move(selected));}
    void SetGrid(bool visible, int plane) { m_engine->SetEditorGrid(visible, plane); }
    void SetEditorIconsVisible(bool visible){m_engine->SetEditorIconsVisible(visible);}
    void SetEditorEntityState(const std::vector<std::string>& hidden,const std::vector<std::string>& blocked){std::vector<UUID> h,b;for(const auto& id:hidden)h.push_back(ParseUuid(id));for(const auto& id:blocked)b.push_back(ParseUuid(id));m_engine->SetEditorEntityState(std::move(h),std::move(b));}
    void SetEditorOrientationVisible(bool visible){m_engine->SetEditorOrientationVisible(visible);}
    bool SetSceneRenderMode(const std::string& mode){return m_engine->SetSceneRenderMode(mode);}
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
        for(std::size_t i=0;i<m_loadedScenes.size();++i)if(i!=m_activeScene){
            const auto path=std::filesystem::temp_directory_path()/("bazzalt-editor-play-"+UUID::Generate().ToString()+".bscene");
            if(!m_engine->SaveSceneAsset(SceneAt(i),path)){CleanupSnapshot();return false;}
            m_extraSnapshots.emplace_back(SceneAt(i).GetUUID(),path);
        }
        // Initialize rendering without gameplay, then start exactly once per Play.
        if (!m_engine->Init(false, false)||!m_engine->StartScripts()) {m_engine->StopScripts();m_engine->LoadScene(m_snapshot);CleanupSnapshot();return false;}
        m_playing = true; m_paused = false; return true;
    }
    void Pause(bool paused) { if (m_playing) {m_paused = paused;if(paused)Runtime::InputAccess::SetActive(false);} }
    void SetGameInputActive(bool focused){Runtime::InputAccess::SetActive(m_playing&&!m_paused&&focused);}
    void GameKey(int key,bool down,bool repeat){if(m_playing&&!m_paused)Runtime::InputAccess::KeyEvent(key,down,repeat);}
    void GameButton(int button,bool down){if(m_playing&&!m_paused)Runtime::InputAccess::ButtonEvent(button,down);}
    void GameMotion(float x,float y,float dx,float dy){if(m_playing&&!m_paused)Runtime::InputAccess::MotionEvent(x,y,dx,dy);}
    void GameScroll(float x,float y){if(m_playing&&!m_paused)Runtime::InputAccess::ScrollEvent(x,y);}
    void GameText(const std::string& text){if(m_playing&&!m_paused)Runtime::InputAccess::TextEvent(text);}
    py::dict InputSnapshot(){const auto& state=*Runtime::InputAccess::GetState();py::dict out;out["active"]=state.Active;py::list keys,down,up;for(int i=1;i<512;++i){if(state.Keys[i])keys.append(i);if(state.KeysDown[i])down.append(i);if(state.KeysUp[i])up.append(i);}out["keys"]=keys;out["down"]=down;out["up"]=up;return out;}
    void Step() { if (m_playing) m_engine->Update(); }
    void Tick() {
        if (m_playing && !m_paused) m_engine->Update();
        else m_engine->RenderEditorFrame();
    }
    void Stop() {
        if (!m_playing) return;
        m_playing = false; m_paused = false;
        Runtime::InputAccess::SetActive(false);
        m_engine->StopScripts();
        if (!m_snapshot.empty()) m_engine->LoadScene(m_snapshot);
        for(const auto& [id,path]:m_extraSnapshots)for(std::size_t i=0;i<m_loadedScenes.size();++i)if(i!=m_activeScene&&SceneAt(i).GetUUID()==id){auto restored=m_engine->LoadSceneAsset(path);if(restored)m_loadedScenes[i].Data=std::move(restored);break;}
        CleanupSnapshot();
    }
    bool IsPlaying() const { return m_playing; }
    bool IsPaused() const { return m_paused; }

    bool ConfigureScripts(const py::list& values) {
        std::vector<Runtime::ScriptBinding> bindings;
        try{for(const py::handle itemHandle:values){const auto item=py::reinterpret_borrow<py::dict>(itemHandle);Runtime::ScriptBinding binding;binding.Module=std::filesystem::u8path(py::str(item["module"]).cast<std::string>());binding.Entity=py::str(item["entity"]).cast<std::string>();binding.TypeName=py::str(item["type"]).cast<std::string>();const auto properties=py::reinterpret_borrow<py::dict>(item["properties"]);for(const auto& pair:properties){const std::string name=py::str(pair.first).cast<std::string>();const py::handle value=pair.second;std::string text;if(py::isinstance<py::bool_>(value))text=value.cast<bool>()?"true":"false";else if(py::isinstance<py::sequence>(value)&&!py::isinstance<py::str>(value)){const auto sequence=py::reinterpret_borrow<py::sequence>(value);for(const auto part:sequence){if(!text.empty())text+=',';text+=py::str(part).cast<std::string>();}}else text=py::str(value).cast<std::string>();binding.Properties.emplace(name,std::move(text));}bindings.push_back(std::move(binding));}}catch(const py::error_already_set& error){m_bridgeError=error.what();return false;}
        m_bridgeError.clear();return m_engine->ConfigureScripts(std::move(bindings));
    }

    bool SyncLuaScripts(const py::list& values) {
        if(m_playing)return false;
        std::vector<std::pair<Entity,ScriptAttachment>> pending;
        try{for(auto handle:values){auto item=py::reinterpret_borrow<py::dict>(handle);
            auto entity=FindEntity(py::str(item["entity"]).cast<std::string>()).second;if(!entity)continue;
            auto source=std::filesystem::u8path(py::str(item["source"]).cast<std::string>());
            auto asset=AssetManager::GetAsset(source);if(!asset||asset->State!=AssetState::Ready){m_bridgeError="Lua asset is not imported";return false;}
            ScriptAttachment attachment;attachment.Source=asset->Id.ToString();attachment.Lua=true;attachment.TypeName=py::str(item["type"]).cast<std::string>();attachment.Enabled=py::cast<bool>(item["enabled"]);
            for(auto pair:py::reinterpret_borrow<py::dict>(item["properties"])){ScriptPropertyValue property;property.Name=py::str(pair.first).cast<std::string>();const auto value=pair.second;
                if(py::isinstance<py::bool_>(value)){property.Type="bool";property.Value=py::cast<bool>(value)?"true":"false";}
                else if(py::isinstance<py::sequence>(value)&&!py::isinstance<py::str>(value)){property.Type="vector";for(auto part:py::reinterpret_borrow<py::sequence>(value)){if(!property.Value.empty())property.Value+=',';property.Value+=py::str(part).cast<std::string>();}}
                else{property.Type=py::isinstance<py::float_>(value)||py::isinstance<py::int_>(value)?"number":"string";property.Value=py::str(value).cast<std::string>();}
                attachment.Properties.push_back(std::move(property));
            }pending.emplace_back(entity,std::move(attachment));
        }}catch(const std::exception& error){m_bridgeError=error.what();return false;}
        for(std::size_t i=0;i<m_loadedScenes.size();++i)for(auto handle:SceneAt(i).GetRegistry().view<ScriptComponents>()){
            auto& entries=SceneAt(i).GetRegistry().get<ScriptComponents>(handle).Values;
            std::erase_if(entries,[](const ScriptAttachment& entry){return entry.Lua;});
        }
        for(auto& [entity,attachment]:pending){auto* scripts=entity.TryGetComponent<ScriptComponents>();if(!scripts)scripts=&entity.AddComponent<ScriptComponents>();scripts->Values.push_back(std::move(attachment));}
        m_bridgeError.clear();return true;
    }
    bool RefreshLuaAssets(const std::vector<std::string>& paths){
        if(m_playing)return false;
        for(const auto& path:paths)if(!m_engine->RefreshLuaAsset(std::filesystem::u8path(path)))return false;
        return true;
    }

    py::list LuaSceneScripts() {
        py::list result;
        for(std::size_t i=0;i<m_loadedScenes.size();++i){auto& scene=SceneAt(i);
            for(auto handle:scene.GetRegistry().view<ScriptComponents>())for(const auto& attachment:scene.GetRegistry().get<ScriptComponents>(handle).Values){
                if(!attachment.Lua)continue;UUID id;if(!UUID::TryParse(attachment.Source,id))continue;
                auto asset=AssetManager::GetAsset(id);if(!asset)continue;
                py::dict value,properties;value["entity"]=scene.GetEntity(static_cast<Entity::Id>(handle)).GetUUID().ToString();
                const auto path=asset->SourcePath.u8string();value["source"]=std::string(reinterpret_cast<const char*>(path.data()),path.size());value["type"]=attachment.TypeName;value["enabled"]=attachment.Enabled;
                for(const auto& property:attachment.Properties){
                    if(property.Type=="bool")properties[py::str(property.Name)]=py::bool_(property.Value=="true");
                    else if(property.Type=="number"||property.Type=="float"){try{properties[py::str(property.Name)]=py::float_(std::stod(property.Value));}catch(const std::exception&){properties[py::str(property.Name)]=property.Value;}}
                    else properties[py::str(property.Name)]=property.Value;
                }
                value["properties"]=properties;result.append(value);
            }
        }return result;
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
        for(const auto& [_,path]:m_extraSnapshots){std::error_code error;std::filesystem::remove(path,error);}m_extraSnapshots.clear();
        if (m_snapshot.empty()) return;
        std::error_code error; std::filesystem::remove(m_snapshot, error); m_snapshot.clear();
    }

    std::unique_ptr<Runtime::Engine> m_engine;
    std::filesystem::path m_projectPath;
    std::filesystem::path m_snapshot;
    std::vector<std::pair<UUID,std::filesystem::path>> m_extraSnapshots;
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
        .def("supported_rendering_backends", &Bazzalt::EditorBridge::EditorHost::SupportedRenderingBackends)
        .def("configure_rendering_backend", &Bazzalt::EditorBridge::EditorHost::ConfigureRenderingBackend)
        .def("project_info", &Bazzalt::EditorBridge::EditorHost::ProjectInfo)
        .def("update_project_info", &Bazzalt::EditorBridge::EditorHost::UpdateProjectInfo)
        .def("asset_directory", &Bazzalt::EditorBridge::EditorHost::AssetDirectory)
        .def("asset_info", &Bazzalt::EditorBridge::EditorHost::AssetInfo)
        .def("refresh_assets", &Bazzalt::EditorBridge::EditorHost::RefreshAssets)
        .def("texture_asset_info", &Bazzalt::EditorBridge::EditorHost::TextureAssetInfo)
        .def("save_texture_asset", &Bazzalt::EditorBridge::EditorHost::SaveTextureAsset)
        .def("used_shader_assets", &Bazzalt::EditorBridge::EditorHost::UsedShaderAssets)
        .def("entities", &Bazzalt::EditorBridge::EditorHost::Entities)
        .def("loaded_scenes", &Bazzalt::EditorBridge::EditorHost::LoadedScenes)
        .def("entity_details", &Bazzalt::EditorBridge::EditorHost::EntityDetails)
        .def("entity_pose", &Bazzalt::EditorBridge::EditorHost::EntityPose)
        .def("restore_transforms", &Bazzalt::EditorBridge::EditorHost::RestoreTransforms)
        .def("entity_count", &Bazzalt::EditorBridge::EditorHost::EntityCount)
        .def("statistics", &Bazzalt::EditorBridge::EditorHost::Statistics)
        .def("set_statistics_text", &Bazzalt::EditorBridge::EditorHost::SetStatisticsText)
        .def("has_active_camera", &Bazzalt::EditorBridge::EditorHost::HasActiveCamera)
        .def("has_game_output", &Bazzalt::EditorBridge::EditorHost::HasGameOutput)
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
        .def("scene_environment", &Bazzalt::EditorBridge::EditorHost::SceneEnvironmentInfo)
        .def("set_scene_environment", &Bazzalt::EditorBridge::EditorHost::SetSceneEnvironment)
        .def("set_environment_import_settings", &Bazzalt::EditorBridge::EditorHost::SetEnvironmentImportSettings)
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
        .def("pick_primitive", &Bazzalt::EditorBridge::EditorHost::PickPrimitive)
        .def("raycast_editor", &Bazzalt::EditorBridge::EditorHost::RaycastEditor)
        .def("set_object_hover", &Bazzalt::EditorBridge::EditorHost::SetObjectHover)
        .def("set_selection_outline", &Bazzalt::EditorBridge::EditorHost::SetSelectionOutline)
        .def("set_grid", &Bazzalt::EditorBridge::EditorHost::SetGrid)
        .def("set_editor_icons_visible", &Bazzalt::EditorBridge::EditorHost::SetEditorIconsVisible)
        .def("set_editor_entity_state", &Bazzalt::EditorBridge::EditorHost::SetEditorEntityState)
        .def("set_editor_orientation_visible", &Bazzalt::EditorBridge::EditorHost::SetEditorOrientationVisible)
        .def("set_scene_render_mode", &Bazzalt::EditorBridge::EditorHost::SetSceneRenderMode)
        .def("configure_scripts", &Bazzalt::EditorBridge::EditorHost::ConfigureScripts)
        .def("sync_lua_scripts", &Bazzalt::EditorBridge::EditorHost::SyncLuaScripts)
        .def("lua_scene_scripts", &Bazzalt::EditorBridge::EditorHost::LuaSceneScripts)
        .def("refresh_lua_assets", &Bazzalt::EditorBridge::EditorHost::RefreshLuaAssets)
        .def("play", &Bazzalt::EditorBridge::EditorHost::Play)
        .def("pause", &Bazzalt::EditorBridge::EditorHost::Pause)
        .def("step", &Bazzalt::EditorBridge::EditorHost::Step)
        .def("tick", &Bazzalt::EditorBridge::EditorHost::Tick)
        .def("stop", &Bazzalt::EditorBridge::EditorHost::Stop)
        .def("is_playing", &Bazzalt::EditorBridge::EditorHost::IsPlaying)
        .def("is_paused", &Bazzalt::EditorBridge::EditorHost::IsPaused)
        .def("rename_asset_directory", &Bazzalt::EditorBridge::EditorHost::RenameAssetDirectory)
        .def("set_game_input_active", &Bazzalt::EditorBridge::EditorHost::SetGameInputActive)
        .def("game_key", &Bazzalt::EditorBridge::EditorHost::GameKey)
        .def("game_text", &Bazzalt::EditorBridge::EditorHost::GameText)
        .def("game_button", &Bazzalt::EditorBridge::EditorHost::GameButton)
        .def("game_motion", &Bazzalt::EditorBridge::EditorHost::GameMotion)
        .def("game_scroll", &Bazzalt::EditorBridge::EditorHost::GameScroll)
        .def("input_snapshot", &Bazzalt::EditorBridge::EditorHost::InputSnapshot);
}
