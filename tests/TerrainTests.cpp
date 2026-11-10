#include "Bazzalt/Terrain.h"
#include "Bazzalt/Detail/PropertyList.h"
#include "Bazzalt/Serialization.h"
#include "Bazzalt/Scene.h"
#include "Terrain/TerrainSerialization.h"
#include "Terrain/TerrainRenderer.h"
#include "Runtime/Engine.h"
#include "Runtime/LuaRuntime.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Physics.h"
#include "Bazzalt/Material.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/MaterialInstance.h>
#include <utils/EntityManager.h>
#include <thread>
#include <fstream>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace Bazzalt;
struct SystemAccess:System {static void Tick(System& system,Scene& scene){constexpr auto method=&SystemAccess::OnUpdate;(system.*method)(scene,0);}};
static void CurvedSplines(){
    Terrain terrain;
    TerrainSpline spline;
    spline.Points={{-4,2,0},{4,2,0}};
    spline.OutTangents={{2,0,4},{}};
    spline.InTangents={{},{-2,0,4}};
    spline.GenerateSurface=true;
    terrain.Splines.push_back(spline);
    auto path=TerrainGeometry::SampleSpline(spline);
    assert(path.size()==17&&std::abs(path[8].Z-3)<.001f);
    assert((path.front()-spline.Points.front()).Length()<.001f);
    assert((path.back()-spline.Points.back()).Length()<.001f);
    auto ribbon=TerrainGeometry::BuildSpline(terrain,0);
    assert(ribbon.Vertices.size()==path.size()*2);
    terrain.ApplySpline(0);
    assert(terrain.GetHeight(0,3)>1.9f);
    ComponentSerializationRegistry registry;Runtime::RegisterTerrainComponents(registry);
    Scene scene;auto source=scene.CreateEntity("Curve");source.AddComponent<Terrain>()=terrain;
    auto target=scene.CreateEntity("Roundtrip");PropertyMap fields;
    auto descriptor=registry.Find("Bazzalt.Terrain");assert(descriptor->Serialize(source,fields));
    assert(descriptor->Deserialize(target,fields,1));
    auto& restored=target.GetComponent<Terrain>().Splines.front();
    assert((restored.OutTangents.front()-spline.OutTangents.front()).Length()<.001f);
    for(auto it=fields.begin();it!=fields.end();){
        if(it->first.ends_with(".In")||it->first.ends_with(".Out"))it=fields.erase(it);else ++it;
    }
    assert(descriptor->Deserialize(target,fields,1));
    assert(TerrainGeometry::SampleSpline(target.GetComponent<Terrain>().Splines.front()).size()==2);
    terrain.Splines.front().InTangents.pop_back();assert(!terrain.IsValid());
}
static void ExtendedTerrain(){
    Terrain edgeCave;
    TerrainVolume tunnel;tunnel.Center={5,-1,0};tunnel.Radius={2,2,2};edgeCave.Volumes.push_back(tunnel);
    auto tunnelHit=TerrainGeometry::Raycast(edgeCave,{10,-1,0},{-1,0,0});
    assert(tunnelHit&&tunnelHit->Position.X<3.2f&&tunnelHit->Position.X>2.8f);
    auto baseHit=TerrainGeometry::Raycast(edgeCave,{4,-30,0},{0,1,0});
    assert(baseHit&&std::abs(baseHit->Position.Y+20)<.001f);
    Terrain terrain;
    terrain.Resize(513);
    assert(terrain.Resolution==513&&terrain.IsValid());
    terrain.Resize(9);
    for(unsigned i=1;i<12;++i)terrain.AddLayer("Layer");
    assert(terrain.Layers.size()==12&&terrain.IsValid());
    Terrain neighbour;
    neighbour.ImportHeightmap(0,{3,4,3,4},2,2);
    terrain.MatchEdge(neighbour,TerrainEdge::Right,Mat4::Translation({10,0,0}));
    assert(std::abs(terrain.GetHeight(5,0)-3)<.001f);
    auto revision=terrain.Revision;
    bool rejected=false;
    try{terrain.MatchEdge(neighbour,TerrainEdge::Right,Mat4::Translation({20,0,0}));}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected&&terrain.Revision==revision&&std::abs(terrain.GetHeight(5,0)-3)<.001f);
    Terrain surface;
    TerrainSpline road;
    road.Points={{-4,2,0},{0,2,0},{4,2,1}};
    road.GenerateSurface=true;
    surface.Splines.push_back(road);
    auto ribbon=TerrainGeometry::BuildSpline(surface,0);
    assert(!ribbon.Indices.empty());
    for(std::size_t i=0;i<ribbon.Indices.size();i+=3){
        auto a=ribbon.Vertices[ribbon.Indices[i]].Position,b=ribbon.Vertices[ribbon.Indices[i+1]].Position,c=ribbon.Vertices[ribbon.Indices[i+2]].Position;
        assert(Vec3::Cross(b-a,c-a).Y>0);
    }
    auto hit=TerrainGeometry::Raycast(surface,{-2,10,0},{0,-1,0});
    assert(hit&&std::abs(hit->Position.Y-2.02f)<.01f);
    Terrain cave;
    cave.Resize(17);
    TerrainVolume cut;
    cut.Center={0,-1,0};cut.Radius={2,2,2};
    cave.Volumes.push_back(cut);
    assert(cave.IsValid());
    hit=TerrainGeometry::Raycast(cave,{.1f,10,.1f},{0,-1,0});
    assert(hit&&hit->Position.Y< -2.8f&&hit->Normal.Y>.8f);
    hit=TerrainGeometry::Raycast(cave,{0,-1,0},{1,0,0});
    assert(hit&&std::abs(hit->Position.X-2)<.1f&&hit->Normal.X<-.9f);
    cave.Volumes[0].Operation=TerrainVolumeOperation::Add;
    cave.Volumes[0].Center={0,2,0};
    hit=TerrainGeometry::Raycast(cave,{.1f,10,.1f},{0,-1,0});
    assert(hit&&hit->Position.Y>3.8f);
    ComponentSerializationRegistry registry;
    Runtime::RegisterTerrainComponents(registry);
    Scene scene;auto entity=scene.CreateEntity("Extended terrain");
    cave.Splines.push_back(road);entity.AddComponent<Terrain>()=cave;
    auto descriptor=registry.Find("Bazzalt.Terrain");PropertyMap fields;
    assert(descriptor->Serialize(entity,fields));
    auto copy=scene.CreateEntity("Copy");assert(descriptor->Deserialize(copy,fields,1));
    assert(copy.GetComponent<Terrain>().Volumes.size()==1&&copy.GetComponent<Terrain>().Splines[0].GenerateSurface);
    // Older scenes have none of the newly optional fields.
    for(auto it=fields.begin();it!=fields.end();){
        if(it->first.starts_with("Volumes.")||it->first=="VerticalRange"||it->first=="VerticalSamples"||it->first=="Splines.0.GenerateSurface"||it->first=="Splines.0.SurfaceCollision"||it->first=="Splines.0.SurfaceOffset"||it->first=="Splines.0.TextureScale"||it->first=="Splines.0.Material"||it->first=="Splines.0.Color")it=fields.erase(it);else ++it;
    }
    assert(descriptor->Deserialize(copy,fields,1));
    assert(copy.GetComponent<Terrain>().Volumes.empty()&&!copy.GetComponent<Terrain>().Splines[0].GenerateSurface);
    Runtime::Engine host;
    auto ground=host.GetScene().CreateEntity("Volumetric ground");
    ground.AddComponent<Terrain>()=cave;
    ground.AddComponent<MeshCollider>().Convex=false;
    auto collision=host.GetScene().GetPhysics().Raycast({.1f,10,.1f},{0,-1,0},20);
    assert(collision&&collision.Point.Y>3.8f);
    ground.GetComponent<Terrain>()=surface;
    collision=host.GetScene().GetPhysics().Raycast({-2,10,0},{0,-1,0},20);
    assert(collision&&std::abs(collision.Point.Y-2.02f)<.01f);
}
static void Rendering(bool gpu){
    Runtime::Engine materialHost;
    Runtime::RenderBackend backend;assert(backend.Initialize(!gpu));
    {
        Scene scene;auto e=scene.CreateEntity("Ground");auto& t=e.AddComponent<Terrain>();auto& system=scene.AddSystem<Runtime::TerrainSystem>(backend);SystemAccess::Tick(system,scene);
        assert(backend.GetAssets().GetEditorMeshes().size()==16);auto before=backend.GetAssets().GetUploadCounts();SystemAccess::Tick(system,scene);assert(backend.GetAssets().GetUploadCounts()==before);
        TerrainBrush brush;brush.Radius=.5f;t.ApplyBrush(TerrainTool::Raise,{},brush);SystemAccess::Tick(system,scene);auto after=backend.GetAssets().GetUploadCounts();assert(after[0]>before[0]&&after[0]-before[0]<16);
        t.Vegetation.emplace_back();t.MarkDirty();SystemAccess::Tick(system,scene); // Empty picker is a valid authoring state.
        t.Vegetation[0].Model=UUID::Generate();t.Instances.push_back({});t.MarkDirty();SystemAccess::Tick(system,scene);assert(backend.GetAssets().GetEditorMeshes().size()==16); // Missing model never aborts rendering.
        t.Enabled=false;SystemAccess::Tick(system,scene);assert(backend.GetAssets().GetEditorMeshes().empty());t.Enabled=true;SystemAccess::Tick(system,scene);assert(backend.GetAssets().GetEditorMeshes().size()==16);
        // Underside must draw the heightfield, not just a grid of LOD skirts.
        auto checkTwoSided=[&](){
            auto& manager=backend.GetEngine().getRenderableManager();unsigned checked=0;
            backend.GetScene().forEach([&](utils::Entity entity){if(auto renderable=manager.getInstance(entity)){
                auto* material=manager.getMaterialInstanceAt(renderable,0);
                assert(material->isDoubleSided());
                assert(material->getCullingMode()==filament::backend::CullingMode::NONE);++checked;
            }});assert(checked>=16);
        };
        checkTwoSided();
        if(gpu){
            auto red=Material::Create(ShaderPreset::Unlit),blue=Material::Create(ShaderPreset::Unlit);
            red.SetColor("baseColor",{1,0,0,1});blue.SetColor("baseColor",{0,0,1,1});
            t.Layers[0].Material=red.GetAssetUUID();t.AddLayer("Blue");t.Layers[1].Material=blue.GetAssetUUID();
            t.Layers[0].Weights.resize(t.Resolution*t.Resolution);t.Layers[1].Weights.resize(t.Resolution*t.Resolution);
            for(unsigned z=0;z<t.Resolution;++z)for(unsigned x=0;x<t.Resolution;++x){float weight=.5f;t.Layers[0].Weights[z*t.Resolution+x]=1-weight;t.Layers[1].Weights[z*t.Resolution+x]=weight;}
            TerrainVolume opening;opening.Center={0,-1,0};opening.Radius={2,2,2};t.Volumes.push_back(opening);t.MarkDirty();SystemAccess::Tick(system,scene);
            checkTwoSided();
            auto& engine=backend.GetEngine();auto light=engine.getEntityManager().create();filament::LightManager::Builder(filament::LightManager::Type::SUN).color({1,1,1}).intensity(50000).direction({0,-1,-.4f}).build(engine,light);backend.GetScene().addEntity(light);
            assert(backend.CreateViewport(101,0,Runtime::RenderBackend::ViewportKind::Scene,256,256,1,true));backend.SetSceneCamera(101,8,7,8,0,0,0);backend.SetEditorGrid(false,1);backend.SetEditorOrientationVisible(false);
            std::optional<Runtime::ViewportFrame> frame;for(unsigned i=0;i<120&&!frame;++i){backend.Render();backend.WaitForViewportFrame(101);engine.pumpMessageQueues();std::this_thread::sleep_for(std::chrono::milliseconds(10));frame=backend.CapturedFrame(101,0);}assert(frame&&frame->Pixels.size()==256*256*4);
            unsigned redPixels=0,bluePixels=0;
            for(std::size_t i=0;i<frame->Pixels.size();i+=4){auto r=frame->Pixels[i],b=frame->Pixels[i+2];if(r>b+30)++redPixels;if(b>r+30)++bluePixels;}
            if(redPixels<=50||bluePixels<=50)std::cerr<<"Terrain blend pixels red="<<redPixels<<" blue="<<bluePixels<<" renderables="<<backend.GetAssets().GetEditorMeshes().size()<<"\n";
            assert(redPixels>50&&bluePixels>50);
            for(unsigned sample=1;sample<=48;++sample){
                backend.Render(0);backend.WaitForViewportFrame(101);engine.pumpMessageQueues();std::this_thread::sleep_for(std::chrono::milliseconds(17));
                if(auto updated=backend.CapturedFrame(101,frame->Sequence))frame=std::move(updated);
            }
            unsigned mixedPixels=0;
            for(std::size_t i=0;i<frame->Pixels.size();i+=4)if(frame->Pixels[i]>80&&frame->Pixels[i+2]>80)++mixedPixels;
            assert(mixedPixels>500);
            unsigned low=255,high=0;for(std::size_t i=0;i<frame->Pixels.size();i+=4){low=std::min(low,unsigned(frame->Pixels[i]));high=std::max(high,unsigned(frame->Pixels[i]));}assert(high>low+20);
            std::ofstream image("dist/terrain-preview.ppm",std::ios::binary);image<<"P6\n256 256\n255\n";for(int y=255;y>=0;--y)for(unsigned x=0;x<256;++x)image.write(reinterpret_cast<const char*>(frame->Pixels.data()+(y*256+x)*4),3);
            image.close();assert(redPixels>50&&bluePixels>50);float redFraction=float(redPixels)/(redPixels+bluePixels);assert(redFraction>.35f&&redFraction<.65f);
            auto capture=[&](){
                for(unsigned sample=0;sample<3;++sample){
                    auto sequence=frame->Sequence;backend.Render(0);backend.WaitForViewportFrame(101);engine.pumpMessageQueues();
                    auto updated=backend.CapturedFrame(101,sequence);assert(updated);frame=std::move(updated);
                }
            };
            auto borderPixels=[&](bool hover){unsigned pixels=0;
                for(std::size_t i=0;i<frame->Pixels.size();i+=4){auto r=frame->Pixels[i],g=frame->Pixels[i+1],b=frame->Pixels[i+2];
                    if(b>230&&(hover?(r>90&&r<160&&g>175&&g<225):(r<65&&g>70&&g<130)))++pixels;
                }return pixels;
            };
            backend.SetEditorOutline({e.GetUUID()},{});capture();assert(borderPixels(false)>50);
            backend.SetEditorOutline({}, {e.GetUUID()});capture();assert(borderPixels(true)>50);
            backend.SetEditorOutline({},{});
            backend.DestroyViewport(101);backend.GetScene().remove(light);engine.destroy(light);engine.getEntityManager().destroy(light);
            // A fresh underside view with no skirts cannot pass by drawing only
            // the chunk walls or reusing the previous camera's TAA history.
            t.Volumes.clear();t.SkirtDepth=0;t.MarkDirty();SystemAccess::Tick(system,scene);
            assert(backend.CreateViewport(102,0,Runtime::RenderBackend::ViewportKind::Scene,256,256,1,true));backend.SetSceneCamera(102,8,-30,8,0,-20,0);
            frame.reset();for(unsigned sample=0;sample<3;++sample){backend.Render(0);backend.WaitForViewportFrame(102);engine.pumpMessageQueues();frame=backend.CapturedFrame(102,0);}
            assert(frame);unsigned underside=0;
            for(std::size_t i=0;i<frame->Pixels.size();i+=4)if((frame->Pixels[i]>100||frame->Pixels[i+2]>100)&&frame->Pixels[i+1]<80)++underside;
            assert(underside>2000);backend.DestroyViewport(102);
        }
    }
    backend.Shutdown();
}
static void LuaAndScene(){
    auto folder=std::filesystem::temp_directory_path()/("bazzalt-terrain-"+UUID::Generate().ToString());std::filesystem::create_directories(folder);
    {
        Runtime::Engine engine;auto e=engine.GetScene().CreateEntity("Scripted ground");auto input=folder/"TerrainTool.lua",output=folder/"TerrainTool.blua";
        {std::ofstream file(input);file<<R"lua(
local B = Bazzalt
local Tool = COMPONENT('Tool')
TAB('Brush')
Tool.Values = PROPERTY('IntList', {1,2})
function Tool:OnCreate()
  assert(self.Values[1] == 4 and self.Values[2] == 5)
  local t = self.Entity:AddComponent('Terrain')
  assert(t:IsValid() and t.Size.X == 10)
  local brush = B.TerrainBrush.new()
  brush.Radius = 2; brush.Strength = 2
  t:ApplyBrush(B.TerrainTool.Raise, B.Vec2.new(0,0), brush, 0.5)
  assert(math.abs(t:GetHeight(0,0)-1) < 0.0001)
  t:ImportHeightmap(0, {0,1,0,1}, 2, 2, 2)
  local hit = B.TerrainGeometry.Raycast(t, B.Vec3.new(0,20,0), B.Vec3.new(0,-1,0))
  assert(hit and math.abs(hit.Position.Y-1) < 0.0001)
  local cave = B.TerrainVolume.new()
  cave.Center = B.Vec3.new(3,-2,3)
  cave.Radius = B.Vec3.new(0.5,0.5,0.5)
  t.Volumes:add(cave)
  assert(#t.Volumes == 1 and t:IsValid())
  local ribbon = B.TerrainSpline.new()
  ribbon.Points:add(B.Vec3.new(-2,3,0))
  ribbon.Points:add(B.Vec3.new(2,3,0))
  ribbon.InTangents:add(B.Vec3.new(0,0,0))
  ribbon.InTangents:add(B.Vec3.new(-1,0,2))
  ribbon.OutTangents:add(B.Vec3.new(1,0,2))
  ribbon.OutTangents:add(B.Vec3.new(0,0,0))
  assert(#B.TerrainGeometry.SampleSpline(ribbon,8) == 9)
  ribbon.GenerateSurface = true
  t.Splines:add(ribbon)
  assert(#B.TerrainGeometry.BuildSpline(t,0).Indices > 0)
  self.Entity:SetComponent('Terrain',t)
end
return Tool
)lua";}
        Runtime::LuaRuntime lua;std::string error;assert(Runtime::LuaRuntime::Import(input,output,error));Runtime::ScriptBinding binding;binding.Module=output;binding.Entity=e.GetUUID().ToString();binding.TypeName="Tool";binding.Properties["Values"]="4,5";assert(lua.Configure({binding},error));if(!lua.Start(error)){std::cerr<<error;assert(false);}assert(e.HasComponent<Terrain>()&&std::abs(e.GetComponent<Terrain>().GetHeight(0,0)-1)<1e-5f);lua.Stop();
        auto path=folder/"Terrain.bscene";assert(engine.SaveSceneAsset(engine.GetScene(),path));auto loaded=engine.LoadSceneAsset(path);assert(loaded&&loaded->FindEntityByName("Scripted ground").HasComponent<Terrain>());
    }
    std::filesystem::remove_all(folder);
}
int main(int argc,char** argv){
    FloatList numeric;assert(Detail::ReadPropertyList("1,2.5,-3",numeric)&&numeric.size()==3);assert(!Detail::ReadPropertyList("1,2,",numeric));assert(!Detail::ReadPropertyList("NaN",numeric));assert(Detail::ReadPropertyList("",numeric)&&numeric.empty());IntList integers;assert(!Detail::ReadPropertyList("1.5",integers));assert(Detail::ReadPropertyList("1,-2",integers));assert(Detail::WritePropertyList(integers)=="1,-2");
    Terrain t;assert(t.IsValid());assert(t.Size.X==10&&t.GetHeight(0,0)==0);
    TerrainBrush b;b.Radius=2;b.Strength=2;t.ApplyBrush(TerrainTool::Raise,{},b);assert(std::abs(t.GetHeight(0,0)-2)<1e-5f);assert(t.GetHeight(5,5)==0);
    auto revision=t.Revision;t.ApplyBrush(TerrainTool::Lower,{},b,.5f);assert(t.Revision>revision&&std::abs(t.GetHeight(0,0)-1)<1e-5f);
    b.TargetHeight=3;b.Strength=1;t.ApplyBrush(TerrainTool::Flatten,{},b);assert(std::abs(t.GetHeight(0,0)-3)<1e-5f);t.Layers[0].Heights[t.Resolution*t.Resolution/2]=4;t.MarkDirty();t.ApplyBrush(TerrainTool::Smooth,{},b);assert(t.GetHeight(0,0)<4);
    t.ImportHeightmap(0,{0,1,0,1},2,2,4);assert(std::abs(t.GetHeight(0,0)-2)<1e-5f&&t.GetNormal(0,0).Y>0);
    auto layer=t.AddLayer("Rock");assert(layer==1);t.ImportHeightmap(layer,{2},1,1);assert(std::abs(t.GetHeight(0,0)-4)<1e-5f);t.Layers[layer].Blend=TerrainBlend::Maximum;t.MarkDirty();assert(std::abs(t.GetHeight(0,0)-2)<1e-5f);
    b.Layer=layer;t.ApplyBrush(TerrainTool::Paint,{},b);assert(t.Layers[layer].Weights[t.Resolution*t.Resolution/2]>0);t.RemoveLayer(layer);
    t.Resize(33);assert(t.Resolution==33&&t.IsValid()&&std::abs(t.GetHeight(0,0)-2)<1e-5f);
    auto mesh=TerrainGeometry::BuildChunk(t,0,0);auto coarse=TerrainGeometry::BuildChunk(t,0,0,3);assert(mesh.Vertices.size()>coarse.Vertices.size());
    for(std::size_t i=0;i<mesh.Indices.size();i+=3){const auto& a=mesh.Vertices[mesh.Indices[i]];const auto& b=mesh.Vertices[mesh.Indices[i+1]];const auto& c=mesh.Vertices[mesh.Indices[i+2]];auto normal=Vec3::Cross(b.Position-a.Position,c.Position-a.Position).Normalized();if(std::abs(a.Normal.Y)<.01f)assert(Vec3::Dot(normal,a.Normal)>.99f);}
    const auto hit=TerrainGeometry::Raycast(t,{0,20,0},{0,-1,0});assert(hit&&std::abs(hit->Position.Y-2)<1e-5f);assert(!TerrainGeometry::Raycast(t,{20,20,0},{0,-1,0}));
    const auto side=TerrainGeometry::Raycast(t,{10,-5,0},{-1,0,0});assert(side&&std::abs(side->Position.X-t.Size.X*.5f)<.001f&&side->Normal.X>.99f);
    const auto bottom=TerrainGeometry::Raycast(t,{0,-30,0},{0,1,0});assert(bottom&&std::abs(bottom->Position.Y-t.VerticalRange.X)<.001f&&bottom->Normal.Y<-.99f);
    b.Layer=0;b.Radius=1;t.ApplyBrush(TerrainTool::Hole,{},b);assert(!TerrainGeometry::Raycast(t,{.01f,20,.01f},{0,-1,0}));t.ApplyBrush(TerrainTool::FillHole,{},b);assert(TerrainGeometry::Raycast(t,{.01f,20,.01f},{0,-1,0}));
    TerrainSpline road;road.Points={{-4,1,0},{4,1,0}};road.Width=2;t.Splines.push_back(road);t.ApplySpline(0);assert(std::abs(t.GetHeight(0,0)-1)<1e-5f);
    Terrain other;other.ImportHeightmap(0,{5},1,1);t.ApplyBoolean(other,Mat4{},TerrainBoolean::Union);assert(std::abs(t.GetHeight(0,0)-5)<1e-5f);
    TerrainVegetation foliage;foliage.Model=UUID::Generate();foliage.Density=.1f;t.Vegetation.push_back(foliage);t.GenerateVegetation(0);auto instances=t.Instances;assert(!instances.empty());t.GenerateVegetation(0);assert(t.Instances.size()==instances.size());for(std::size_t i=0;i<instances.size();++i)assert((instances[i].Position-t.Instances[i].Position).Length()<1e-6f);
    bool rejected=false;try{t.Resize(32);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    ComponentSerializationRegistry registry;Runtime::RegisterTerrainComponents(registry);Scene scene;auto entity=scene.CreateEntity("Terrain");entity.AddComponent<Terrain>()=t;
    PropertyMap fields;auto descriptor=registry.Find("Bazzalt.Terrain");assert(descriptor&&descriptor->Serialize(entity,fields));Scene copy;auto clone=copy.CreateEntity("Terrain");assert(descriptor->Deserialize(clone,fields,1));auto& loaded=clone.GetComponent<Terrain>();assert(loaded.IsValid()&&loaded.Layers[0].Heights==t.Layers[0].Heights&&loaded.Instances.size()==t.Instances.size());
    fields["Layers.Count"]="999999999";assert(!descriptor->Deserialize(clone,fields,1));assert(clone.GetComponent<Terrain>().IsValid());
    {
        Runtime::Engine host;auto& scene=host.GetScene();auto ground=scene.CreateEntity("Terrain collision");auto& terrain=ground.AddComponent<Terrain>();ground.AddComponent<MeshCollider>().Convex=false;
        auto hit=scene.GetPhysics().Raycast({0,10,0},{0,-1,0},20);assert(hit&&hit.Object==ground&&std::abs(hit.Point.Y)<.01f);
        terrain.ImportHeightmap(0,{2},1,1);hit=scene.GetPhysics().Raycast({0,10,0},{0,-1,0},20);assert(hit&&std::abs(hit.Point.Y-2)<.01f);
        hit=scene.GetPhysics().Raycast({10,-5,0},{-1,0,0},20);assert(hit&&hit.Object==ground&&std::abs(hit.Point.X-5)<.01f);
        hit=scene.GetPhysics().Raycast({0,-30,0},{0,1,0},20);assert(hit&&hit.Object==ground&&std::abs(hit.Point.Y+20)<.01f);
    }
    CurvedSplines();ExtendedTerrain();Rendering(argc>1&&std::string(argv[1])=="--gpu");LuaAndScene();std::cout<<"Terrain tests passed\n";
}
