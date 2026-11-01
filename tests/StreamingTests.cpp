#include <iostream>
#include "Runtime/LuaRuntime.h"
#include "Runtime/Engine.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include <filament/Engine.h>
#include <cassert>
#include <fstream>
#include <thread>
#include <chrono>
using namespace Bazzalt;
struct Marker:Component{int Value=3;};
int main(){
    auto directory=std::filesystem::temp_directory_path()/std::filesystem::path(u8"bazzalt_streaming_\u0442\u0435\u0441\u0442");
    std::filesystem::remove_all(directory);std::filesystem::create_directories(directory/"Assets");
    {std::ofstream file(directory/"Game.bproject");file<<"FormatVersion: 1\nProjectUUID: 00000000-0000-0001-0000-000000000001\nName: Streaming\nAssetDirectory: Assets\nStartupScene: ''\nProperties: {}\n";}
    {std::ofstream file(directory/"Assets"/"triangle.bin",std::ios::binary);const float positions[]={0,0,0,1,0,0,0,1,0};file.write(reinterpret_cast<const char*>(positions),sizeof(positions));}
    {std::ofstream file(directory/"Assets"/"triangle.gltf");file<<R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0,"buffers":[{"uri":"triangle.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";}
    {std::ofstream file(directory/"Assets"/"streaming.lua");file<<R"(local Behavior = COMPONENT("StreamingCheck")
function Behavior:OnCreate()
    local B=Bazzalt
    assert(B.StreamingSettings.new().EntitiesPerTick==128)
    assert(B.SceneManager.GetStreamingSettings().EntitiesPerTick==1)
    assert(B.ResourceLoadHandle.new():GetStatus()==B.AsyncLoadStatus.Failed)
    assert(B.SceneLoadHandle.new():IsDone())
    assert(B.Graphics.IsInstancingEnabled())
    B.Graphics.SetInstancingEnabled(false);assert(not B.Graphics.IsInstancingEnabled());B.Graphics.SetInstancingEnabled(true)
    local cell=B.StreamingCell.new();assert(cell.UnloadDistance>cell.LoadDistance)
    local transform=self.Entity:GetComponent("Transform");transform.Position.X=77;self.Entity:SetComponent("Transform",transform)
end
return Behavior)";}
    {std::ofstream file(directory/"Assets"/"cell.lua");file<<R"(local Behavior=COMPONENT("CellBehavior")
function Behavior:OnCreate()
    local t=self.Entity:GetComponent("Transform");t.Position.X=5;self.Entity:SetComponent("Transform",t)
end
function Behavior:OnUpdate()
    local t=self.Entity:GetComponent("Transform");t.Position.Y=t.Position.Y+1;self.Entity:SetComponent("Transform",t)
end
function Behavior:OnDestroy()
    local e=Bazzalt.SceneManager.GetActiveScene():FindEntityByName("Keep me")
    local t=e:GetComponent("Transform");t.Position.Z=55;e:SetComponent("Transform",t)
end
return Behavior)";}
    Runtime::Engine engine;
    SceneLoadHandle cancellation;
    const auto mainThread=std::this_thread::get_id();
    engine.GetSceneSerializer().GetComponents().Register<Marker>("Game.Marker",1,
        [](const Marker& v,PropertyMap& p){p["Value"]=std::to_string(v.Value);},
        [mainThread,&cancellation](Marker& v,const PropertyMap& p,std::uint32_t version){assert(std::this_thread::get_id()==mainThread);if(version!=1)return false;v.Value=std::stoi(p.at("Value"));if(v.Value==88)assert(cancellation.Cancel());return v.Value!=77;});
    Scene chunk;auto parent=chunk.CreateEntity(UUID{11,1},"Cell parent");parent.GetComponent<Transform>().Position={1,0,0};parent.AddComponent<PrimitiveObject>();parent.AddComponent<Marker>();
    auto child=chunk.CreateEntity(UUID{11,2},"Cell child");child.SetParent(parent,false);child.GetComponent<Transform>().Position={2,0,0};
    assert(engine.SaveSceneAsset(chunk,directory/"Assets"/"cell.bscene"));
    Scene replacement;replacement.CreateEntity(UUID{12,1},"Replacement");assert(engine.SaveSceneAsset(replacement,directory/"Assets"/"replacement.bscene"));
    Scene bad;bad.CreateEntity(UUID{13,1},"Rejected").AddComponent<Marker>().Value=77;assert(engine.SaveSceneAsset(bad,directory/"Assets"/"bad.bscene"));
    Scene scriptedCell;auto scriptedCellEntity=scriptedCell.CreateEntity(UUID{15,1},"Cell behavior");ScriptAttachment behavior;behavior.Source="Assets/cell.lua";behavior.TypeName="CellBehavior";behavior.Lua=true;scriptedCellEntity.AddComponent<ScriptComponents>().Values.push_back(behavior);assert(engine.SaveSceneAsset(scriptedCell,directory/"Assets"/"scripted.bscene"));
    Scene cancelAtActivation;cancelAtActivation.CreateEntity(UUID{16,1},"Cancelled activation").AddComponent<Marker>().Value=88;assert(engine.SaveSceneAsset(cancelAtActivation,directory/"Assets"/"cancel.bscene"));
    assert(engine.LoadProject(directory/"Game.bproject",false));assert(engine.Init(true,false));
    auto permanent=engine.GetScene().CreateEntity("Keep me");auto permanentId=permanent.GetUUID();const auto originalScene=engine.GetScene().GetUUID();
    auto asset=AssetManager::GetAsset(directory/"Assets"/"cell.bscene");assert(asset&&asset->State==AssetState::Ready);
    auto model=AssetManager::GetAsset(directory/"Assets"/"triangle.gltf");assert(model&&model->State==AssetState::Ready);
    auto settings=SceneManager::GetStreamingSettings();settings.EntitiesPerTick=1;SceneManager::SetStreamingSettings(settings);
    auto tick=[&]{engine.Update(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));};
    auto wait=[&](auto& handle){for(int i=0;i<5000&&!handle.IsDone();++i)tick();assert(handle.IsDone());if(handle.GetStatus()==AsyncLoadStatus::Failed)std::cerr<<"Streaming failure: "<<handle.GetError()<<"\n";assert(handle.GetStatus()!=AsyncLoadStatus::Failed);};
    auto resource=AssetManager::LoadAsync(asset->Id);wait(resource);assert(resource.GetData()&&!resource.GetData()->empty());assert(resource.GetProgress()==1);
    auto cancelled=SceneManager::LoadSceneAsync(directory/"Assets"/"replacement.bscene");assert(cancelled.Cancel());wait(cancelled);assert(cancelled.GetStatus()==AsyncLoadStatus::Cancelled);assert(engine.GetScene().GetUUID()==originalScene);
    Transform offset;offset.Position={100,0,0};auto loaded=SceneManager::LoadWorldCellAsync(asset->Id,offset);wait(loaded);
    auto root=engine.GetScene().GetEntity(loaded.GetRootUUID());assert(root);auto inCell=engine.GetScene().GetEntity(UUID{11,2});assert(inCell);assert(std::abs(inCell.GetWorldTransform().Position.X-103)<.001f);assert(engine.GetScene().GetEntity(UUID{11,1}).GetComponent<Marker>().Value==3);
    auto duplicate=SceneManager::LoadWorldCellAsync(asset->Id,offset);assert(duplicate.GetRootUUID()==loaded.GetRootUUID());assert(engine.GetScene().GetEntityCount()==5);
    permanent.SetParent(root,true);assert(SceneManager::UnloadWorldCell(asset->Id));tick();assert(loaded.GetStatus()==AsyncLoadStatus::Unloaded);assert(engine.GetScene().GetEntity(permanentId));assert(!engine.GetScene().GetEntity(UUID{11,1}));
    engine.GetScene().CreateEntity(UUID{11,1},"Conflict");auto conflict=SceneManager::LoadWorldCellAsync(asset->Id);for(int i=0;i<5000&&!conflict.IsDone();++i)tick();assert(conflict.GetStatus()==AsyncLoadStatus::Failed);assert(engine.GetScene().GetEntity(UUID{11,1}).GetComponent<Name>().Value=="Conflict");engine.GetScene().DestroyEntity(engine.GetScene().GetEntity(UUID{11,1}));
    StreamingCell cell;cell.SceneAsset=asset->Id;cell.Offset=offset;cell.LoadDistance=10;cell.UnloadDistance=20;assert(SceneManager::RegisterStreamingCell(cell));assert(!SceneManager::RegisterStreamingCell(cell));
    SceneManager::SetStreamingPosition({100,0,0});for(int i=0;i<5000&&!engine.GetScene().GetEntity(UUID{11,2});++i)tick();assert(engine.GetScene().GetEntity(UUID{11,2}));SceneManager::SetStreamingPosition({121,0,0});for(int i=0;i<100&&engine.GetScene().GetEntity(UUID{11,2});++i)tick();assert(!engine.GetScene().GetEntity(UUID{11,2}));assert(SceneManager::UnregisterStreamingCell(asset->Id));
    auto badAsset=AssetManager::GetAsset(directory/"Assets"/"bad.bscene");assert(badAsset);auto rollback=SceneManager::LoadWorldCellAsync(badAsset->Id);for(int i=0;i<5000&&!rollback.IsDone();++i)tick();assert(rollback.GetStatus()==AsyncLoadStatus::Failed);assert(!engine.GetScene().GetEntity(UUID{13,1}));assert(engine.GetScene().GetEntityCount()==2);
    assert(engine.StartScripts());auto scriptedAsset=AssetManager::GetAsset(directory/"Assets"/"scripted.bscene");assert(scriptedAsset);auto runningCell=SceneManager::LoadWorldCellAsync(scriptedAsset->Id);wait(runningCell);auto behaviorEntity=engine.GetScene().GetEntity(UUID{15,1});assert(behaviorEntity.GetComponent<Transform>().Position.X==5);assert(behaviorEntity.GetComponent<Transform>().Position.Y>0);assert(SceneManager::UnloadWorldCell(scriptedAsset->Id));tick();assert(engine.GetScene().GetEntity(permanentId).GetComponent<Transform>().Position.Z==55);engine.StopScripts();
    cancellation=SceneManager::LoadSceneAsync(directory/"Assets"/"cancel.bscene");wait(cancellation);assert(cancellation.GetStatus()==AsyncLoadStatus::Cancelled);assert(engine.GetScene().GetUUID()==originalScene);
    auto request=SceneManager::LoadSceneAsync(directory/"Assets"/"replacement.bscene",false);for(int i=0;i<5000&&request.GetStatus()!=AsyncLoadStatus::Ready;++i)tick();assert(request.GetStatus()==AsyncLoadStatus::Ready);for(int i=0;i<5;++i)tick();assert(engine.GetScene().GetUUID()==originalScene);assert(request.SetActivationAllowed(true));wait(request);assert(engine.GetScene().GetUUID()==replacement.GetUUID());assert(!engine.GetScene().GetEntity(permanentId));assert(engine.GetScene().FindEntityByName("Replacement"));
    auto invalid=SceneManager::LoadSceneAsync(directory/"missing.bscene");for(int i=0;i<5000&&!invalid.IsDone();++i)tick();assert(invalid.GetStatus()==AsyncLoadStatus::Failed);assert(engine.GetScene().GetUUID()==replacement.GetUUID());auto script=AssetManager::GetAsset(directory/"Assets"/"streaming.lua");assert(script&&script->State==AssetState::Ready);
    auto scripted=engine.GetScene().CreateEntity("Lua API");Runtime::LuaRuntime lua;std::string error;Runtime::ScriptBinding binding;binding.Module=script->CachePath;binding.TypeName="StreamingCheck";binding.Entity=scripted.GetUUID().ToString();assert(lua.Configure({binding},error));if(!lua.Start(error)){std::cerr<<error<<"\n";assert(false);}assert(scripted.GetComponent<Transform>().Position.X==77);lua.Stop();
    engine.Shutdown(true);resource={};Jobs::WaitAll();
    Runtime::RenderBackend backend;assert(backend.Initialize(true));auto& assets=backend.GetAssets();assets.SetAsynchronousLoading(true);Mesh mesh;mesh.MeshAsset=model->Id;
    auto modelHandle=assets.CreateMesh(mesh,{},UUID{14,1});assert(modelHandle==Runtime::RenderAssets::PendingHandle);
    for(int i=0;i<5000&&modelHandle==Runtime::RenderAssets::PendingHandle;++i){assets.Update();modelHandle=assets.CreateMesh(mesh,{},UUID{14,1});std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(modelHandle!=Runtime::RenderAssets::PendingHandle&&modelHandle!=Runtime::RenderAssets::InvalidHandle);assets.DestroyMesh(modelHandle);for(int i=0;i<4;++i)assets.Update();
    std::vector<Runtime::RenderAssets::Handle> instances;PrimitiveObject primitive;
    for(int i=0;i<100;++i)instances.push_back(assets.CreatePrimitive(primitive));auto counts=assets.GetInstancingResourceCounts();assert(counts[0]==1&&counts[1]==1);assert(backend.GetEngine().isAutomaticInstancingEnabled());
    primitive.Color={1,0,0,1};assets.UpdatePrimitive(instances[0],Mat4{},primitive);counts=assets.GetInstancingResourceCounts();assert(counts[0]==1&&counts[1]==2);assets.DestroyPrimitive(instances[0]);counts=assets.GetInstancingResourceCounts();assert(counts[0]==1&&counts[1]==1);
    for(std::size_t i=1;i<instances.size();++i)assets.DestroyPrimitive(instances[i]);counts=assets.GetInstancingResourceCounts();assert(counts[0]==0&&counts[1]==0);backend.Shutdown();std::filesystem::remove_all(directory);
}
