#include "Bazzalt/Scene.h"
#include "Bazzalt/Material.h"
#include "Bazzalt/Components/HLOD.h"
#include "Bazzalt/Components/LODTransition.h"
#include "Bazzalt/Components/Impostor.h"
#include "Runtime/Engine.h"
#include "Rendering/LODVisibility.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/ModelGeometry.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/RenderSystems.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/LightManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <cassert>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <string_view>
#include <cstdio>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
static void Selection(){
    LOD lod;lod.Levels={{.5f,{}},{.1f,{}}};assert(lod.IsValid());assert(lod.SelectLevel(.8f)==0);assert(lod.SelectLevel(.3f)==1);assert(lod.SelectLevel(.01f)==-1);
    assert(lod.SelectLevel(.52f,1)==1);assert(lod.SelectLevel(.56f,1)==0);assert(lod.SelectLevel(.46f,0)==0);assert(lod.SelectLevel(.44f,0)==1);assert(lod.SelectLevel(.105f,-1)==-1);assert(lod.SelectLevel(.12f,-1)==1);
    lod.Cull=false;assert(lod.SelectLevel(0)==1);lod.ForceLevel(-2);assert(lod.SelectLevel(1)==-1);lod.Enabled=false;assert(lod.SelectLevel(0)==0);lod.Enabled=true;lod.ForceLevel(1);assert(lod.SelectLevel(1)==1);lod.ForceLevel(-1);
    assert(std::abs(lod.GetScreenHeight(Mat4::Identity(),{0,0,10},2)-.1f)<1e-6f);assert(lod.GetScreenHeight(Mat4::Identity(),{0,0,10},.2f,true)==lod.GetScreenHeight(Mat4::Identity(),{0,0,100},.2f,true));
    auto id=UUID::Generate();lod.Levels[0].Roots={id};lod.Levels[1].Roots={id};assert(!lod.IsValid());
}
static void HierarchyAndSerialization(){
    Engine host;auto& scene=host.GetScene();auto root=scene.CreateEntity("LOD group"),high=scene.CreateEntity("High"),low=scene.CreateEntity("Low");high.SetParent(root,false);low.SetParent(root,false);high.AddComponent<PrimitiveObject>();low.AddComponent<PrimitiveObject>();auto& lod=root.AddComponent<LOD>();lod.Size=2;lod.Levels={{.4f,{high.GetUUID()}},{.05f,{low.GetUUID()}}};
    LODViewState near,far;EvaluateLOD(scene,{0,0,2},1,false,near);EvaluateLOD(scene,{0,0,10},1,false,far);assert(!near.Hidden.contains(high.GetUUID())&&near.Hidden.contains(low.GetUUID()));assert(far.Hidden.contains(high.GetUUID())&&!far.Hidden.contains(low.GetUUID()));
    EvaluateLOD(scene,{0,0,100},1,false,far);assert(far.Hidden.contains(high.GetUUID())&&far.Hidden.contains(low.GetUUID()));assert(high.GetComponent<PrimitiveObject>().Enabled&&high.GetComponent<PrimitiveObject>().Visible);
    lod.ForceLevel(0);EvaluateLOD(scene,{0,0,100},1,false,far);assert(!far.Hidden.contains(high.GetUUID()));lod.ForceLevel(-1);
    auto saved=lod.Levels;lod.Levels[1].Roots={UUID::Generate()};EvaluateLOD(scene,{0,0,2},1,false,near);assert(near.Hidden.empty());lod.Levels=saved;EvaluateLOD(scene,{0,0,2},1,false,near);assert(near.Warnings.empty());
    auto file=std::filesystem::temp_directory_path()/("lod-"+UUID::Generate().ToString()+".bscene");assert(host.SaveSceneAsset(scene,file));auto loaded=host.LoadSceneAsset(file);std::filesystem::remove(file);assert(loaded);auto& copy=loaded->FindEntityByName("LOD group").GetComponent<LOD>();assert(copy.Levels.size()==2&&copy.Levels[1].Roots[0]==low.GetUUID());
    auto old=high.GetComponent<Hierarchy>().Parent;high.GetComponent<Hierarchy>().Parent=high.GetUUID();bool rejected=false;try{high.GetWorldMatrix();}catch(const std::logic_error&){rejected=true;}assert(rejected);high.GetComponent<Hierarchy>().Parent=old;
}
static void AdvancedSelection(){
    Engine host;auto& scene=host.GetScene();auto root=scene.CreateEntity("Cluster"),source=scene.CreateEntity("Sources"),proxy=scene.CreateEntity("Proxy"),high=scene.CreateEntity("High"),low=scene.CreateEntity("Low");source.SetParent(root,false);proxy.SetParent(root,false);high.SetParent(source,false);low.SetParent(source,false);
    auto& cluster=root.AddComponent<HLOD>();cluster.Size=4;cluster.ScreenHeight=.2f;cluster.Sources={source.GetUUID()};cluster.Proxies={proxy.GetUUID()};assert(cluster.IsConfigured());auto& inner=source.AddComponent<LOD>();inner.Levels={{.05f,{high.GetUUID()}},{0,{low.GetUUID()}}};inner.Cull=false;
    LODViewState near,far;EvaluateLOD(scene,{0,0,2},1,false,near,0);EvaluateLOD(scene,{0,0,20},1,false,far,0);assert(!near.Hidden.contains(source.GetUUID())&&near.Hidden.contains(proxy.GetUUID()));assert(far.Hidden.contains(high.GetUUID())&&far.Hidden.contains(low.GetUUID())&&!far.Hidden.contains(proxy.GetUUID()));assert(!far.Levels.contains(source.GetUUID()));
    auto& transition=root.AddComponent<LODTransition>();transition.Duration=.5f;transition.Easing=LODEasing::Linear;
    EvaluateLOD(scene,{0,0,2},1,false,near,1);EvaluateLOD(scene,{0,0,20},1,false,near,2);EvaluateLOD(scene,{0,0,20},1,false,near,2.125);
    assert(near.HasFades&&std::abs(near.Masks.at(source.GetUUID()).Minimum-.25f)<1e-6f&&std::abs(near.Masks.at(proxy.GetUUID()).Maximum-.25f)<1e-6f);
    for(unsigned pixel=0;pixel<64;++pixel){float threshold=(pixel+.5f)/64;auto a=near.Masks.at(source.GetUUID()),b=near.Masks.at(proxy.GetUUID());assert(int(threshold>=a.Minimum&&threshold<a.Maximum)+int(threshold>=b.Minimum&&threshold<b.Maximum)==1);}
    EvaluateLOD(scene,{0,0,2},1,false,near,2.1875);assert(std::abs(near.Masks.at(source.GetUUID()).Minimum-.125f)<1e-6f);EvaluateLOD(scene,{0,0,2},1,false,near,3);assert(!near.HasFades&&near.Hidden.contains(proxy.GetUUID()));
    EvaluateLOD(scene,{0,0,20},1,false,near,4);EvaluateLOD(scene,{0,0,20},1,false,near,4.125);near.RejectUnsupported(low.GetUUID(),false);near.Resolve();assert(near.Hidden.contains(high.GetUUID())&&!near.Hidden.contains(proxy.GetUUID())&&near.Masks.empty());
    cluster.ForceLevel(0);EvaluateLOD(scene,{0,0,200},1,false,near,5);assert(!near.HasFades&&!near.Hidden.contains(source.GetUUID()));
    auto& impostor=proxy.AddComponent<Impostor>();impostor.Atlas=UUID::Generate();impostor.Columns=4;impostor.Rows=2;impostor.ViewCount=8;impostor.Pivot={.25f,.75f};assert(impostor.IsValid());assert(impostor.SelectFrame(Mat4{},Vec3{0,0,5})==0&&impostor.SelectFrame(Mat4{},Vec3{5,0,0})==2&&impostor.SelectFrame(Mat4{},Vec3{0,0,-5})==4);auto rect=impostor.GetAtlasRect(5);assert(rect.X==.25f&&rect.Y==.5f&&rect.Z==.25f&&rect.W==.5f);
    auto matrix=impostor.GetMatrix(Mat4::Translation({1,2,3})*Mat4::Scaling({2,3,1}),Mat4::Translation({0,5,10}));assert((matrix.TransformPoint({impostor.Pivot.X,impostor.Pivot.Y,0})-Vec3{1,2,3}).Length()<1e-5f);assert(matrix.TransformDirection({0,1,0}).Normalized().Y>.999f);
    auto file=std::filesystem::temp_directory_path()/("hlod-"+UUID::Generate().ToString()+".bscene");assert(host.SaveSceneAsset(scene,file));auto loaded=host.LoadSceneAsset(file);std::filesystem::remove(file);assert(loaded);auto copy=loaded->FindEntityByName("Cluster").GetComponent<HLOD>();assert(copy.Sources==cluster.Sources&&copy.Proxies==cluster.Proxies&&loaded->FindEntityByName("Cluster").GetComponent<LODTransition>().Duration==.5f);assert(loaded->FindEntityByName("Proxy").GetComponent<Impostor>().Atlas==impostor.Atlas);
    assert(host.GetSceneSerializer().GetComponents().IsEntityReference("Bazzalt.HLOD","Sources.0"));assert(!host.GetSceneSerializer().GetComponents().IsEntityReference("Bazzalt.HLOD","Sources.Count"));
}
static float Brute(const PrimitiveGeometry& g,Vec3 origin,Vec3 direction){float closest=1000;for(std::size_t i=0;i<g.Indices.size();i+=3){Vec3 v[3];for(int j=0;j<3;++j){auto& p=g.Vertices[g.Indices[i+j]].Position;v[j]={p[0],p[1],p[2]};}auto e1=v[1]-v[0],e2=v[2]-v[0],p=Vec3::Cross(direction,e2);float d=Vec3::Dot(e1,p);if(std::abs(d)<1e-8f)continue;float r=1/d;auto offset=origin-v[0];float u=Vec3::Dot(offset,p)*r;if(u<0||u>1)continue;auto q=Vec3::Cross(offset,e1);float w=Vec3::Dot(direction,q)*r;if(w<0||u+w>1)continue;float hit=Vec3::Dot(e2,q)*r;if(hit>=0&&hit<closest)closest=hit;}return closest;}
static void GeometryAndMath(){
    for(int shape=0;shape<7;++shape){PrimitiveObject p;p.Shape=PrimitiveShape(shape);auto cached=GetCachedPrimitiveGeometry(p);auto original=BuildPrimitiveGeometry(p);assert(cached->Indices==original.Indices);p.Color={1,0,0,1};assert(GetCachedPrimitiveGeometry(p)==cached);assert(cached->Picking);
        for(int ray=0;ray<32;++ray){Vec3 origin{(ray%8-3.2f)*.23f,(ray/8-1.2f)*.25f,3},direction{0,0,-1};float hit=1000;cached->Picking->Raycast(origin,direction,hit,1e-8f);assert(std::abs(hit-Brute(original,origin,direction))<1e-4f);}}
    assert(GetPrimitiveCacheStatistics()[2]<=32u*1024u*1024u);
    for(int i=0;i<1000;++i){Vec3 p{float(i%7),-.2f*i,.01f*i},s{(i%2?-1.f:1.f)*(.1f+i%5),.3f+i%3,2.f};auto q=Quaternion::FromEuler({.013f*i,.017f*i,.023f*i});auto expected=Mat4::Translation(p)*Mat4::Rotation(q)*Mat4::Scaling(s),actual=Mat4::Transform(p,q,s);for(int j=0;j<16;++j)assert(std::abs(actual.Values[j]-expected.Values[j])<1e-4f);}
}
static void Resources(bool gpu){
    Engine host;RenderBackend backend;if(gpu)assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(!gpu));
    {
        auto& assets=backend.GetAssets();PrimitiveObject primitive;auto handle=assets.CreatePrimitive(primitive);auto id=UUID::Generate();assets.SetEditorOwner(handle,id);assets.UpdatePrimitive(handle,Mat4::Identity(),primitive);auto counts=assets.GetUploadCounts();for(int i=0;i<100;++i)assets.UpdatePrimitive(handle,Mat4::Identity(),primitive);assert(assets.GetUploadCounts()==counts);
        primitive.Color={.2f,.4f,.6f,1};assets.UpdatePrimitive(handle,Mat4::Translation({1,0,0}),primitive);assert(assets.GetUploadCounts()[0]==counts[0]+1&&assets.GetUploadCounts()[2]==counts[2]+1);
        primitive.CastShadows=false;assets.UpdatePrimitive(handle,Mat4::Translation({1,0,0}),primitive);assert(assets.GetUploadCounts()[1]==counts[1]+1);
        const auto visible=backend.GetScene().getRenderableCount();std::unordered_set<UUID> hidden{id};assets.BeginLODView(hidden);assert(backend.GetScene().getRenderableCount()==visible-1);assets.EndLODView();assert(backend.GetScene().getRenderableCount()==visible);assets.DestroyPrimitive(handle);
        Scene lights;lights.AddSystem<LightSystem>(backend);auto entity=lights.CreateEntity();entity.AddComponent<Light>();lights.GetSystem<LightSystem>().Synchronize(lights);auto uploads=backend.GetStatistics().at("light_uploads");for(int i=0;i<100;++i)lights.GetSystem<LightSystem>().Synchronize(lights);assert(backend.GetStatistics().at("light_uploads")==uploads);entity.GetComponent<Light>().Intensity=500;lights.GetSystem<LightSystem>().Synchronize(lights);assert(backend.GetStatistics().at("light_uploads")==uploads+1);entity.GetComponent<Transform>().Position.X=1;lights.GetSystem<LightSystem>().Synchronize(lights);assert(backend.GetStatistics().at("light_uploads")==uploads+2);
    }
    if(gpu){
        Scene scene;scene.AddSystem<CameraSystem>(backend);scene.AddSystem<PrimitiveSystem>(backend);backend.SetGuiScene(&scene);
        auto root=scene.CreateEntity(),high=scene.CreateEntity(),low=scene.CreateEntity();high.SetParent(root,false);low.SetParent(root,false);auto red=Material::Create(ShaderPreset::Unlit),blue=Material::Create(ShaderPreset::Unlit);red.SetColor("baseColor",{1,0,0,1});blue.SetColor("baseColor",{0,0,1,1});high.AddComponent<PrimitiveObject>().MaterialAsset=red.GetAssetUUID();low.AddComponent<PrimitiveObject>().MaterialAsset=blue.GetAssetUUID();auto& lod=root.AddComponent<LOD>();lod.Size=2;lod.Levels={{.4f,{high.GetUUID()}},{.02f,{low.GetUUID()}}};
        auto near=scene.CreateEntity(),far=scene.CreateEntity();near.GetComponent<Transform>().Position={0,0,3};far.GetComponent<Transform>().Position={0,0,20};TextureDescriptor d;d.Width=d.Height=64;d.Mipmaps=false;auto a=Texture::Create(d),b=Texture::Create(d);
        auto setup=[&](Entity e,Texture t){auto& c=e.AddComponent<Camera>();c.PostProcessing.Enabled=false;c.SetRenderTarget(t);};setup(near,a);setup(far,b);scene.GetSystem<PrimitiveSystem>().Synchronize(scene);scene.GetSystem<CameraSystem>().Synchronize(scene);auto& engine=backend.GetEngine();
        auto read=[&](Entity e,bool average=false){auto* previous=backend.GetCameraOutputTarget(e.GetUUID());filament::RenderTarget* target=nullptr;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);do{backend.Render();engine.flushAndWait();engine.pumpMessageQueues();target=backend.GetCameraOutputTarget(e.GetUUID());if(target&&target!=previous)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);assert(target&&target!=previous);std::vector<std::uint8_t> pixels(64*64*4);bool done=false;backend.GetRenderer().readPixels(target,0,0,64,64,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* ptr){*static_cast<bool*>(ptr)=true;},&done});engine.flushAndWait();engine.pumpMessageQueues();assert(done);if(average){Vec3 sum;for(int y=20;y<44;++y)for(int x=20;x<44;++x){auto at=(y*64+x)*4;sum+=Vec3{float(pixels[at]),float(pixels[at+1]),float(pixels[at+2])};}return sum/576.f;}auto at=(32*64+32)*4;return Vec3{float(pixels[at]),float(pixels[at+1]),float(pixels[at+2])};};
        auto n=read(near),f=read(far);assert(n.X>n.Z+30&&f.Z>f.X+30);near.GetComponent<Transform>().Position.Z=4.4f;scene.GetSystem<CameraSystem>().Synchronize(scene);n=read(near);assert(n.X>n.Z+30);lod.ForceLevel(0);f=read(far);assert(f.X>f.Z+30);lod.ForceLevel(-2);n=read(near);assert(n.Length()<5);lod.Enabled=false;n=read(near);assert(n.X>n.Z+30);
        auto& fade=root.AddComponent<LODTransition>();fade.Duration=1;lod.Enabled=true;lod.ForceLevel(-1);high.GetComponent<PrimitiveObject>().Size={4,4,4};low.GetComponent<PrimitiveObject>().Size={4,4,4};scene.GetSystem<PrimitiveSystem>().Synchronize(scene);near.GetComponent<Transform>().Position.Z=3;scene.GetSystem<CameraSystem>().Synchronize(scene);n=read(near);assert(n.X>n.Z+30);
        near.GetComponent<Transform>().Position.Z=20;scene.GetSystem<CameraSystem>().Synchronize(scene);read(near);std::this_thread::sleep_for(std::chrono::milliseconds(350));n=read(near,true);if(!(n.X>5&&n.Z>5))std::fprintf(stderr,"Fade readback: %f %f %f\n",n.X,n.Y,n.Z);assert(n.X>5&&n.Z>5);f=read(far);assert(f.Z>f.X+30);std::this_thread::sleep_for(std::chrono::milliseconds(1100));n=read(near,true);assert(n.Z>n.X+20);
        high.GetComponent<PrimitiveObject>().Visible=false;low.GetComponent<PrimitiveObject>().Visible=false;scene.GetSystem<PrimitiveSystem>().Synchronize(scene);
        auto sprite=scene.CreateEntity();auto& imp=sprite.AddComponent<Impostor>();imp.Size={4,4};imp.Color={0,1,0,1};TextureDescriptor atlasDescriptor;atlasDescriptor.Width=atlasDescriptor.Height=4;atlasDescriptor.Mipmaps=false;atlasDescriptor.ClearColor={1,1,1,1};auto atlas=Texture::Create(atlasDescriptor);imp.Atlas=atlas.GetAssetUUID();n=read(near);assert(n.Y>n.X+30&&n.Y>n.Z+30);
        imp.Color={1,1,1,1};imp.Columns=1;imp.Rows=2;imp.ViewCount=2;auto targets=backend.GetAssets().GetTextureTargets(atlas.GetAssetUUID());for(auto* texture:{targets.Color,targets.History}){auto* data=new std::vector<std::uint8_t>(4*4*4);for(int y=0;y<4;++y)for(int x=0;x<4;++x){auto at=(y*4+x)*4;(*data)[at]=y<2?255:0;(*data)[at+1]=0;(*data)[at+2]=y<2?0:255;(*data)[at+3]=255;}texture->setImage(engine,0,{data->data(),data->size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* p){delete static_cast<std::vector<std::uint8_t>*>(p);},data});}
        far.GetComponent<Transform>().Position.Z=-20;far.GetComponent<Transform>().Rotation=Quaternion::FromEuler({0,3.14159265358979323846f,0});scene.GetSystem<CameraSystem>().Synchronize(scene);n=read(near);f=read(far);assert(n.X>n.Z+30&&f.Z>f.X+30);imp.Color={0,1,0,1};
        atlas.Resize(8,8);n=read(near);assert(n.Y>n.X+30);imp.Color.W=.1f;n=read(near);assert(n.Length()<5);imp.Color.W=1;imp.Enabled=false;n=read(near);assert(n.Length()<5);imp.Enabled=true;n=read(near);assert(n.Y>n.X+30);atlas.Destroy();backend.Render();engine.flushAndWait();engine.pumpMessageQueues();
        scene.RemoveSystem<PrimitiveSystem>();scene.RemoveSystem<CameraSystem>();backend.SetGuiScene(nullptr);a.Destroy();b.Destroy();
    }
    backend.Shutdown();
}
int main(int argc,char** argv){Selection();HierarchyAndSerialization();AdvancedSelection();GeometryAndMath();Resources(argc>1&&std::string_view(argv[1])=="--gpu");}
