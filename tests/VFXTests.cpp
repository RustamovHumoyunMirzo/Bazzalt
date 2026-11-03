#include "Bazzalt/VFX.h"
#include "VFX/VFXInternal.h"
#include "Bazzalt/Systems/ParticleSystem.h"
#include "Bazzalt/Scene.h"
#include "Runtime/Engine.h"
#include "Bazzalt/AssetManager.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderSystems.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Texture.h>
#include <cassert>
#include <fstream>
#include <chrono>
#include <thread>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
static const std::string Source=R"({"version":1,"nodes":[{"id":"spawn","type":"Spawn","values":{"rate":60,"burst":2,"duration":1,"loop":false},"inputs":{}},{"id":"init","type":"Initialize","values":{"lifetimeMin":2,"lifetimeMax":2,"speedMin":0,"speedMax":0,"sizeMin":1,"sizeMax":1},"inputs":{"in":"spawn"}},{"id":"gravity","type":"Gravity","values":{"acceleration":[0,0,0]},"inputs":{"in":"init"}},{"id":"out","type":"Output","values":{},"inputs":{"in":"gravity"}}]})";
static void Simulation(){
 auto graph=VFXGraph::FromSource(Source);assert(graph.IsValid());assert(!VFXGraph::FromSource("{}").IsValid());assert(!VFXGraph::FromSource(R"({"version":2,"nodes":[]})").IsValid());
 auto cyclic=Source;auto at=cyclic.find("\"inputs\":{}");cyclic.replace(at,11,"\"inputs\":{\"in\":\"out\"}");assert(!VFXGraph::FromSource(cyclic).IsValid());
 Scene scene;scene.AddSystem<ParticleSystem>();auto a=scene.CreateEntity(),b=scene.CreateEntity();a.AddComponent<ParticleEmitter>();b.AddComponent<ParticleEmitter>();VFX::SetGraph(a,graph);VFX::SetGraph(b,graph);VFX::Simulate(a,.5f);for(int i=0;i<60;++i)VFX::Simulate(b,1.f/120);auto x=VFX::GetParticles(a),y=VFX::GetParticles(b);assert(!x.empty()&&x.size()==y.size());for(std::size_t i=0;i<x.size();++i){assert(x[i].Id==y[i].Id);assert((x[i].Position-y[i].Position).Length()<1e-5f);assert(std::abs(x[i].Age-y[i].Age)<1e-5f);}
 VFX::Pause(a);double time=VFX::GetStatistics(a).Time;VFX::Simulate(a,.1f);assert(VFX::GetStatistics(a).Time==time);VFX::Resume(a);VFX::Simulate(a,.1f);assert(VFX::GetStatistics(a).Time>time);
 VFX::Restart(a);VFX::Emit(a,3);assert(VFX::GetStatistics(a).Alive==3);auto first=VFX::GetParticles(a);VFX::Restart(a);VFX::Emit(a,3);assert(VFX::GetParticles(a)[0].Rotation==first[0].Rotation);a.GetComponent<ParticleEmitter>().MaxParticles=4;VFX::Emit(a,10);assert(VFX::GetStatistics(a).Alive==4&&VFX::GetStatistics(a).Dropped==9);assert(!VFX::GetEvents(a).empty());VFX::SetGraph(a,graph);assert(VFX::GetEvents(a).empty()&&VFX::GetStatistics(a).Capacity==4);
 VFX::Stop(a);assert(!VFX::GetStatistics(a).Playing&&VFX::GetParticles(a).empty());VFX::Simulate(b,3);assert(VFX::GetParticles(b).empty()&&!VFX::GetStatistics(b).Playing);
 auto c=scene.CreateEntity();auto& emitter=c.AddComponent<ParticleEmitter>();emitter.Space=ParticleSimulationSpace::World;c.GetComponent<Transform>().Position={10,0,0};VFX::SetGraph(c,graph);VFX::Emit(c,1);assert(std::abs(VFX::GetParticles(c)[0].Position.X-10)<1e-6f);c.GetComponent<Transform>().Position.X=20;assert(VFX::GetParticles(c)[0].Position.X==10);
 emitter.Enabled=false;auto before=VFX::GetStatistics(c).Time;VFXAccess::Update(scene,.1f);assert(VFX::GetStatistics(c).Time==before);
 scene.DestroyEntity(c);VFXAccess::Update(scene,.1f);
}
static void Write(const std::filesystem::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;assert(f.good());}
static void Imports(){auto root=std::filesystem::temp_directory_path()/("bazzalt-vfx-"+UUID::Generate().ToString());std::filesystem::create_directories(root/"Assets");auto child=UUID::Generate();auto parent=UUID::Generate();auto import=[&](UUID id){return "{\"version\":1,\"nodes\":[{\"id\":\"import\",\"type\":\"Import\",\"values\":{\"asset\":\""+id.ToString()+"\"},\"inputs\":{}},{\"id\":\"output\",\"type\":\"Output\",\"values\":{},\"inputs\":{\"in\":\"import\"}}]}";};
 Write(root/"Assets/child.vfx",Source);Write(root/"Assets/parent.vfx",import(child));for(auto item:{std::pair{"child.vfx",child},std::pair{"parent.vfx",parent}})Write(root/"Assets"/(std::string(item.first)+".meta"),"FormatVersion: 1\nUUID: \""+item.second.ToString()+"\"\nImporter: Bazzalt.VFX\nImporterVersion: 0\nSourceHash: \"\"\nCachePath: \"\"\nSettings: {}\n");Write(root/"Game.bproject","FormatVersion: 1\nProjectUUID: \""+UUID::Generate().ToString()+"\"\nName: VFX\nAssetDirectory: Assets\nStartupScene: \"\"\n");
 {Engine host;assert(host.LoadProject(root/"Game.bproject",false));auto graph=VFXGraph::Load(parent);assert(graph.IsValid());assert(graph.GetDependencies()==std::vector<UUID>{child});assert(AssetManager::GetDependencies(parent)==std::vector<UUID>{child});auto entity=host.GetScene().CreateEntity();entity.AddComponent<ParticleEmitter>().Graph=parent;auto scenePath=root/"Assets/Main.bscene";assert(host.SaveScene(scenePath));auto loaded=host.LoadSceneAsset(scenePath);assert(loaded&&loaded->GetEntity(entity.GetUUID()).GetComponent<ParticleEmitter>().Graph==parent);Write(root/"Assets/child.vfx",import(parent));assert(host.RefreshAssets());assert(!VFXGraph::Load(parent).IsValid());}
 std::filesystem::remove_all(root);
}
static void GPU(){RenderBackend backend;assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(false));Scene scene;scene.AddSystem<ParticleSystem>();scene.AddSystem<CameraSystem>(backend);backend.SetGuiScene(&scene);auto e=scene.CreateEntity();e.AddComponent<ParticleEmitter>();VFX::SetGraph(e,VFXGraph::FromSource(Source));VFX::Emit(e,1);auto camera=scene.CreateEntity();camera.GetComponent<Transform>().Position={0,0,5};auto& c=camera.AddComponent<Camera>();c.PostProcessing.Enabled=false;TextureDescriptor descriptor;descriptor.Width=descriptor.Height=64;descriptor.Mipmaps=false;auto texture=Texture::Create(descriptor);c.SetRenderTarget(texture);scene.GetSystem<CameraSystem>().Synchronize(scene);auto& engine=backend.GetEngine();for(int i=0;i<5;++i){backend.Render();engine.flushAndWait();engine.pumpMessageQueues();}auto* target=backend.GetCameraOutputTarget(camera.GetUUID());assert(target);std::vector<unsigned char> pixels(64*64*4);bool done=false;backend.GetRenderer().readPixels(target,0,0,64,64,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* p){*static_cast<bool*>(p)=true;},&done});engine.flushAndWait();engine.pumpMessageQueues();assert(done);auto center=(32*64+32)*4;assert(pixels[center]>50&&pixels[center+1]>50);VFX::Stop(e);backend.Render();engine.flushAndWait();engine.pumpMessageQueues();scene.RemoveSystem<CameraSystem>();backend.SetGuiScene(nullptr);texture.Destroy();backend.Shutdown();}
int main(int argc,char**){Simulation();Imports();if(argc>1)GPU();}
