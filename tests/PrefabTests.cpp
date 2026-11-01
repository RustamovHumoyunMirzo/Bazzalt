#include "Bazzalt/Prefab.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/DistanceJoint.h"
#include "Prefab/PrefabAccess.h"
#include "Runtime/Engine.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
struct Custom:Component {float Speed=3;UUID Target;};
int main(int argc,char** argv){try{
 auto folder=std::filesystem::temp_directory_path()/std::filesystem::path(u8"bazzalt-prefab-\u0422\u0435\u0441\u0442")/UUID::Generate().ToString();std::filesystem::create_directories(folder/"Assets");
 {Engine engine;assert(engine.SaveProject(folder/"Game.bproject"));assert(engine.LoadProject(folder/"Game.bproject",false));
 auto& registry=engine.GetSceneSerializer().GetComponents();registry.Register<Custom>("Test.Custom",1,[](const Custom& value,PropertyMap& p){p["Speed"]=std::to_string(value.Speed);p["Target"]=value.Target.ToString();},[](Custom& value,const PropertyMap& p,std::uint32_t version){if(version!=1)return false;value.Speed=std::stof(p.at("Speed"));return UUID::TryParse(p.at("Target"),value.Target);});registry.RegisterEntityReference("Test.Custom","Target");
 auto& scene=engine.GetScene();auto root=scene.CreateEntity("Assembly");auto child=scene.CreateEntity("Child");child.SetParent(root,false);child.AddComponent<PrimitiveObject>();auto& custom=root.AddComponent<Custom>();custom.Target=child.GetUUID();auto& unknown=child.AddComponent<UnresolvedComponents>();unknown.Values.push_back({"Plugin.Future",5,{{"Data","preserve me"}}});root.GetComponent<Transform>().Position={3,2,1};
 auto path=folder/"Assets/Assembly.bprefab";std::string error;assert(PrefabAccess::Save(root,path,engine.GetSceneSerializer(),error));assert(!PrefabAccess::Save(root,path,engine.GetSceneSerializer(),error));assert(engine.RefreshAssets());auto asset=AssetManager::GetAsset(path);assert(asset&&asset->Importer=="Bazzalt.Prefab"&&asset->State==AssetState::Ready);assert(PrefabAccess::Link(root,asset->Id,engine.GetSceneSerializer(),error));assert(!Prefab::HasOverrides(root));auto prefab=Prefab::Load(asset->Id);assert(prefab&&prefab->GetEntityCount()==2);
 auto first=prefab->Instantiate(scene);auto second=scene.InstantiatePrefab(asset->Id);assert(first&&second&&first!=second&&first!=root);auto firstChild=first.GetChildren().front();assert(first.GetComponent<Custom>().Target==firstChild.GetUUID());assert(firstChild.GetComponent<UnresolvedComponents>().Values.front().Properties.at("Data")=="preserve me");assert(Prefab::GetSource(firstChild)==asset->Id);assert(!Prefab::HasOverrides(first));
 first.GetComponent<Transform>().Position={100,0,0};assert(!Prefab::HasOverrides(first));first.GetComponent<Custom>().Speed=9;assert(Prefab::HasOverrides(first));auto firstId=first.GetUUID(),childId=firstChild.GetUUID();
 root.GetComponent<Custom>().Speed=5;root.GetComponent<Name>().Value="Updated Assembly";auto added=scene.CreateEntity("New Source Child");added.SetParent(root,false);added.AddComponent<PrimitiveObject>();assert(PrefabAccess::Save(root,path,engine.GetSceneSerializer(),error,true));assert(engine.RefreshAssets());assert(PrefabAccess::Link(root,asset->Id,engine.GetSceneSerializer(),error));first=Prefab::Refresh(first);second=Prefab::Refresh(second);assert(first.GetUUID()==firstId&&scene.GetEntity(childId));assert(first.GetComponent<Custom>().Speed==9&&second.GetComponent<Custom>().Speed==5);assert(first.GetComponent<Name>().Value=="Updated Assembly"&&first.GetChildren().size()==2);assert(first.GetComponent<Transform>().Position.X==100&&Prefab::HasOverrides(first)&&!Prefab::HasOverrides(second));
 auto detached=second.GetChildren().front();auto detachedId=detached.GetUUID();assert(detached.RemoveParent(true));second=Prefab::Revert(second);assert(scene.GetEntity(detachedId)&&!scene.GetEntity(detachedId).GetParent().GetUUID());assert(second.GetChildren().size()==2);
 first=Prefab::Revert(first);assert(first.GetUUID()==firstId&&first.GetComponent<Custom>().Speed==5&&!Prefab::HasOverrides(first)&&first.GetComponent<Transform>().Position.X==100);assert(first.GetComponent<Custom>().Target==childId);
 auto nestedChild=first.GetChildren().front();auto nestedPath=folder/"Assets/Nested.bprefab";assert(PrefabAccess::Save(nestedChild,nestedPath,engine.GetSceneSerializer(),error));assert(engine.RefreshAssets());assert(PrefabAccess::Link(nestedChild,AssetManager::GetAsset(nestedPath)->Id,engine.GetSceneSerializer(),error));assert(Prefab::HasOverrides(first));bool rejected=false;try{Prefab::Revert(first);}catch(const std::runtime_error&){rejected=true;}assert(rejected&&scene.GetEntity(firstId));assert(Prefab::Unpack(nestedChild));
 assert(engine.SaveScene(folder/"Assets/Main.bscene"));assert(engine.LoadScene(folder/"Assets/Main.bscene"));auto loaded=engine.GetScene().GetEntity(firstId);assert(loaded&&Prefab::GetSource(loaded)==asset->Id&&!Prefab::HasOverrides(loaded));assert(Prefab::Unpack(loaded));assert(!Prefab::GetSource(loaded)&&loaded.GetChildren().size()==2);
 // An invalid source is an import error, and cannot destroy a live instance.
 {std::ofstream bad(path);bad<<"PrefabVersion: 99\n";}assert(engine.RefreshAssets());assert(AssetManager::GetAsset(asset->Id)->State==AssetState::Failed);assert(!Prefab::Load(asset->Id));assert(engine.GetScene().GetEntity(firstId));
 // Runtime-spawned prefabs start their own Lua/native lifecycle at a safe tick.
 auto luaPath=folder/"Assets/Dynamic.lua";{std::ofstream file(luaPath);file<<R"lua(local B=Bazzalt
local W=COMPONENT('DynamicPrefab')
local scene
function W:OnCreate() scene=self.Entity:GetScene();local t=self.Entity:GetWorldTransform();t.Position.X=77;self.Entity:SetWorldTransform(t) end
function W:OnDestroy() if scene then scene:CreateEntity('Prefab destroyed') end end
return W)lua";}
 auto nativePath=folder/"Assets/Native.cpp";{std::ofstream file(nativePath);file<<"// native lifecycle fixture";}assert(engine.RefreshAssets());
 auto luaAsset=AssetManager::GetAsset(luaPath);assert(luaAsset&&luaAsset->State==AssetState::Ready);auto luaSource=engine.GetScene().CreateEntity("Scripted source");ScriptAttachment luaAttachment;luaAttachment.Source=luaAsset->Id.ToString();luaAttachment.TypeName="DynamicPrefab";luaAttachment.Lua=true;luaSource.AddComponent<ScriptComponents>().Values.push_back(luaAttachment);auto luaPrefabPath=folder/"Assets/Scripted.bprefab";assert(PrefabAccess::Save(luaSource,luaPrefabPath,engine.GetSceneSerializer(),error));engine.GetScene().DestroyEntity(luaSource);assert(engine.RefreshAssets());auto luaPrefab=Prefab::Load(AssetManager::GetAsset(luaPrefabPath)->Id);assert(luaPrefab);
 std::optional<Prefab> nativePrefab;auto log=std::filesystem::temp_directory_path()/("bazzalt-prefab-life-"+UUID::Generate().ToString()+".log");
 if(argc>1){auto nativeAsset=AssetManager::GetAsset(nativePath);assert(nativeAsset);auto source=engine.GetScene().CreateEntity("Native source");ScriptAttachment attachment;attachment.Source=nativeAsset->Id.ToString();attachment.TypeName="LifecycleProbe";auto text=log.u8string();attachment.Properties.push_back({"LogPath","std::string",std::string(reinterpret_cast<const char*>(text.data()),text.size())});source.AddComponent<ScriptComponents>().Values.push_back(attachment);auto path=folder/"Assets/Native.bprefab";assert(PrefabAccess::Save(source,path,engine.GetSceneSerializer(),error));engine.GetScene().DestroyEntity(source);assert(engine.RefreshAssets());nativePrefab=Prefab::Load(AssetManager::GetAsset(path)->Id);assert(nativePrefab);PrefabAccess::SetScriptModules({{"LifecycleProbe",std::filesystem::u8path(argv[1])}});}
 assert(engine.Init(true,false));assert(engine.StartScripts());auto spawned=luaPrefab->Instantiate(engine.GetScene());assert(spawned.GetComponent<Transform>().Position.X!=77);Entity nativeSpawn;if(nativePrefab)nativeSpawn=nativePrefab->Instantiate(engine.GetScene());engine.Update(false);assert(spawned.GetComponent<Transform>().Position.X==77);engine.GetScene().DestroyEntity(spawned);if(nativeSpawn)engine.GetScene().DestroyEntity(nativeSpawn);engine.Update(false);assert(engine.GetScene().FindEntityByName("Prefab destroyed"));if(nativePrefab){std::ifstream file(log);std::string text((std::istreambuf_iterator<char>(file)),{});assert(text.find("create:")!=std::string::npos&&text.find("update:")!=std::string::npos&&text.find("destroy:")!=std::string::npos);file.close();std::filesystem::remove(log);}engine.StopScripts();engine.Shutdown();

 }
 std::filesystem::remove_all(folder);std::cout<<"Prefab tests passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
