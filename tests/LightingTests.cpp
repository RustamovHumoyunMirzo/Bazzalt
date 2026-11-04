#include "Bazzalt/Lighting.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/LightProbe.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Serialization.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Components/Lightmap.h"
#include "Bazzalt/Components/ReflectionProbe.h"
#include "Rendering/BakedLightingAtlas.h"
#include "Rendering/LightingFields.h"
#include "Rendering/LightingSky.h"
#include "Rendering/LightingMaterialProvider.h"
#include <gltfio/MaterialProvider.h>
#include <filament/Material.h>
#include <bit>
#include "Runtime/Engine.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Bazzalt/Components/Camera.h"
#include "Rendering/RenderSystems.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/Renderer.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <filament/View.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <limits>
#include <chrono>
#include <thread>
using namespace Bazzalt;
void Wait(const LightBakeHandle& handle){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(handle.GetStatus()==LightBakeStatus::Baking&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(handle.GetStatus()==LightBakeStatus::Complete);assert(handle.GetProgress()==1);}
struct TickSystem : System {static void Tick(System& system,Scene& scene){constexpr auto update=&TickSystem::OnUpdate;(system.*update)(scene,0);}};
int CheckPixels(){
    Runtime::RenderBackend backend;assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(false));backend.SetPresentationSize(96,96);
    {
        Scene scene;auto cameraEntity=scene.CreateEntity("Camera");auto& camera=cameraEntity.AddComponent<Camera>();camera.PostProcessing.Bloom=false;camera.PostProcessing.AmbientOcclusion=false;camera.PostProcessing.AntiAliasingMode=AntiAliasing::None;
        auto& cameras=scene.AddSystem<Runtime::CameraSystem>(backend);TickSystem::Tick(cameras,scene);
        auto handle=backend.GetAssets().CreatePrimitive(PrimitiveObject{});backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-3}),PrimitiveObject{});
        for(unsigned i=0;i<64;++i){auto far=scene.CreateEntity(UUID{0,i+1},"Far probe");far.GetComponent<Transform>().Position={1000.f+float(i)*10,0,0};auto& p=far.AddComponent<LightProbe>();p.Baked=true;p.Radius=1;}
        auto e=scene.CreateEntity(UUID{999,999},"Probe");auto& probe=e.AddComponent<LightProbe>();probe.Baked=true;probe.Radius=20;
        auto& engine=backend.GetEngine();
        {auto* provider=Runtime::CreateLightingMaterialProvider(&engine,{});filament::gltfio::MaterialKey key{};key.hasBaseColorTexture=true;key.baseColorUV=0;key.hasClearCoat=true;key.hasSheen=true;filament::gltfio::UvMap uv{};auto* material=provider->getMaterial(&key,&uv,"GI textured glTF");assert(material&&material->hasParameter("baseColorMap")&&material->hasParameter("bazzaltLightingData"));assert(uv[0]==filament::gltfio::UvSet::UV0);provider->destroyMaterials();delete provider;}
        auto& renderer=backend.GetRenderer();auto* view=backend.GetActiveViews().front();
        auto* color=filament::Texture::Builder().width(96).height(96).levels(1).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::BLIT_SRC).build(engine);
        auto* target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,color).build(engine);view->setRenderTarget(target);renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});
        const auto capture=[&](){backend.SynchronizeBakedLighting(scene);for(int i=0;i<3;++i){renderer.renderStandaloneView(view);engine.flushAndWait();engine.pumpMessageQueues();}std::vector<std::uint8_t> pixels(96*96*4);bool complete=false;renderer.readPixels(target,0,0,96,96,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* p){*static_cast<bool*>(p)=true;},&complete});engine.flushAndWait();engine.pumpMessageQueues();assert(complete);return pixels;};
        const auto dark=capture();probe.Coefficients[0]={100000000,30000000,10000000};const auto lit=capture();Lighting::Clear(scene);const auto cleared=capture();
        unsigned changed=0,restored=0;for(std::size_t i=0;i<dark.size();i+=4){if(int(lit[i])-int(dark[i])>10)++changed;if(std::abs(int(cleared[i])-int(dark[i]))>3)++restored;}
        std::cout<<"Probe GI changed pixels="<<changed<<", clear differences="<<restored<<std::endl;assert(changed>100);assert(restored<100);
        const auto countChanges=[&](const auto& image,const auto& baseline){unsigned n=0;for(std::size_t i=0;i<image.size();i+=4)if(int(image[i])-int(baseline[i])>10)++n;return n;};
        for(auto h:scene.GetRegistry().view<LightProbe>())scene.GetRegistry().get<LightProbe>(h).Enabled=false;
        auto receiver=scene.CreateEntity("GPU lightmap");receiver.GetComponent<Transform>().Position={0,0,-3};receiver.AddComponent<PrimitiveObject>();auto& map=receiver.AddComponent<Lightmap>();map.Resolution=32;map.Padding=1;
        LightBakeSettings settings;settings.Samples=16;settings.Bounces=0;settings.LightmapSamples=1;settings.AmbientRadiance={10000000,3000000,1000000};auto bake=Lighting::BeginBake(scene,settings);Wait(bake);assert(bake.Apply(scene));auto mapped=capture();auto mapChanges=countChanges(mapped,cleared);std::cout<<"Lightmap changed pixels="<<mapChanges<<std::endl;assert(mapChanges>100);
        receiver.GetComponent<Transform>().Position.X=100;auto staleMap=capture();assert(countChanges(staleMap,cleared)<100);scene.DestroyEntity(receiver);
        auto reflectionEntity=scene.CreateEntity("GPU reflection");auto& reflection=reflectionEntity.AddComponent<ReflectionProbe>();reflection.Resolution=reflection.BakedResolution=4;reflection.Baked=true;reflection.Radiance.assign(126,{10000000,3000000,1000000});auto reflected=capture();auto reflectionChanges=countChanges(reflected,cleared);std::cout<<"Reflection changed pixels="<<reflectionChanges<<std::endl;assert(reflectionChanges>100);reflection.Enabled=false;auto noReflection=capture();assert(countChanges(noReflection,cleared)<100);
        view->setRenderTarget(nullptr);engine.destroy(target);engine.destroy(color);backend.GetAssets().DestroyPrimitive(handle);
    }backend.Shutdown();return 0;
}
void CheckExtended(){
    {Scene dense;auto probes=Lighting::CreateProbeGrid(dense,{0,0,0},{99,0,0},100,1,1,.4f);for(auto e:probes){auto& p=e.GetComponent<LightProbe>();p.Baked=true;p.Coefficients[0]={2,1,.5f};}assert(Lighting::Sample(dense,{99,0,0},{0,1,0}).X>.5f);auto atlas=Runtime::BuildBakedLightingAtlas(dense);assert(atlas.Pixels[0]==100);auto before=Runtime::BakedLightingFingerprint(dense);probes.back().GetComponent<LightProbe>().Enabled=false;assert(before!=Runtime::BakedLightingFingerprint(dense));assert(Runtime::BuildBakedLightingAtlas(dense).Pixels[0]==99);}
    {Scene overlap;auto probes=Lighting::CreateProbeGrid(overlap,{},{},513,1,1,10);for(auto e:probes)e.GetComponent<LightProbe>().Baked=true;auto atlas=Runtime::BuildBakedLightingAtlas(overlap);assert(atlas.SpatialFallback);assert(atlas.Pixels[0]==513&&atlas.Pixels[3]==0);}
    std::vector<Vec3> values{{1,-2,3},{.125f,100000,0}},decoded{{9,9,9}};auto text=Runtime::EncodeLightingVectors(values);assert(Runtime::DecodeLightingVectors(text,decoded)&&decoded==values);auto previous=decoded;assert(!Runtime::DecodeLightingVectors(text+"!",decoded)&&decoded==previous);assert(!Runtime::DecodeLightingVectors("AAAAAAAAAAAAAAA!",decoded));
    Scene scene;auto object=scene.CreateEntity("Lightmapped cube");object.AddComponent<PrimitiveObject>().ContributeGI=true;auto& map=object.AddComponent<Lightmap>();map.Resolution=32;map.Padding=1;auto capture=scene.CreateEntity("Reflection");auto& reflection=capture.AddComponent<ReflectionProbe>();reflection.Resolution=4;capture.GetComponent<Transform>().Position={0,2,0};
    LightBakeSettings settings;settings.Samples=16;settings.Bounces=0;settings.LightmapSamples=1;settings.ReflectionSamples=4;settings.AmbientRadiance={3,2,1};auto bake=Lighting::BeginBake(scene,settings);Wait(bake);assert(bake.Apply(scene));assert(map.IsValid()&&map.Baked&&map.Geometry.size()==36&&map.Texels.size()==1024);assert(reflection.IsValid()&&reflection.Baked&&reflection.Radiance.size()==126);assert(std::abs(Lighting::Sample(scene,{0,.5f,0},{0,1,0}).X-3*Pi)<.001f);assert(Lighting::SampleReflection(scene,{0,2,0},{0,1,0}).X>2.99f);reflection.Enabled=false;assert(Lighting::SampleReflection(scene,{0,2,0},{0,1,0}).LengthSquared()==0);reflection.Enabled=true;
    auto atlas=Runtime::BuildBakedLightingAtlas(scene);assert(atlas.Pixels[1]==12&&atlas.Pixels[2]==1);auto fingerprint=Runtime::BakedLightingFingerprint(scene);Lighting::Invalidate(scene);assert(fingerprint!=Runtime::BakedLightingFingerprint(scene));object.GetComponent<Transform>().Position.X=10;assert(Runtime::BuildBakedLightingAtlas(scene).Pixels[1]==0);assert(Lighting::Sample(scene,{0,.5f,0},{0,1,0}).LengthSquared()==0);object.GetComponent<Transform>().Position.X=0;
    Runtime::Engine engine;auto file=std::filesystem::temp_directory_path()/("bazzalt-atlas-"+UUID::Generate().ToString()+".bscene");assert(engine.SaveSceneAsset(scene,file));auto loaded=engine.LoadSceneAsset(file);assert(loaded);assert(loaded->GetEntity(object.GetUUID()).GetComponent<Lightmap>().Texels==map.Texels);assert(loaded->GetEntity(capture.GetUUID()).GetComponent<ReflectionProbe>().Radiance==reflection.Radiance);std::filesystem::remove(file);
    auto stale=Lighting::BeginBake(scene,settings);Wait(stale);map.Padding=0;auto old=map.Texels;assert(!stale.Apply(scene)&&map.Texels==old);map.Padding=1;
    settings.MaxBakeTexels=1;bool rejected=false;try{Lighting::BeginBake(scene,settings);}catch(const std::length_error&){rejected=true;}assert(rejected);Lighting::Clear(scene);assert(!map.Baked&&map.Texels.empty()&&!reflection.Baked&&reflection.Radiance.empty());
    // Six uncompressed HDR KTX faces; malformed data is rejected without invoking the GPU parser.
    std::vector<std::uint8_t> ktx(68+6*4*4*12);const unsigned char magic[]={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};std::copy(std::begin(magic),std::end(magic),ktx.begin());auto word=[&](unsigned at,std::uint32_t value){for(unsigned k=0;k<4;++k)ktx[at+k]=std::uint8_t(value>>(k*8));};word(12,0x04030201);word(16,0x1406);word(20,4);word(24,0x1907);word(28,0x8815);word(32,0x1907);word(36,4);word(40,4);word(52,6);word(56,1);word(64,4*4*12);for(unsigned i=68;i<ktx.size();i+=4)word(i,std::bit_cast<std::uint32_t>(2.f));auto sky=Runtime::DecodeLightingSky(ktx,3);assert(sky.IsValid());assert(std::abs(Runtime::SampleReflectionRadiance(sky,{1,0,0},0).X-6)<.001f);
    auto validKtx=ktx;word(68,std::bit_cast<std::uint32_t>(INFINITY));rejected=false;try{Runtime::DecodeLightingSky(ktx,1);}catch(const std::runtime_error&){rejected=true;}assert(rejected);ktx=validKtx;
    ktx.pop_back();rejected=false;try{Runtime::DecodeLightingSky(ktx,1);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
}
int main(int argc,char**){
    if(argc>1)return CheckPixels();
    CheckExtended();
    LightBakeSettings settings;settings.Samples=128;assert(settings.IsValid());auto invalid=settings;invalid.Samples=0;assert(!invalid.IsValid());invalid=settings;invalid.AmbientRadiance.X=std::numeric_limits<float>::infinity();assert(!invalid.IsValid());
    Scene scene;auto probes=Lighting::CreateProbeGrid(scene,{-1,0,-1},{1,0,1},1,1,1,20);auto& probe=probes[0].GetComponent<LightProbe>();assert(!probe.Baked);
    bool threw=false;try{Lighting::CreateProbeGrid(scene,{},Vec3{1},64,64,64);}catch(const std::invalid_argument&){threw=true;}assert(threw);assert(scene.GetRegistry().view<LightProbe>().size()==1);
    auto sun=scene.CreateEntity("Baked sun");auto& light=sun.AddComponent<Light>();light.Type=LightType::Directional;light.Mode=LightMode::Baked;light.Intensity=100;sun.GetComponent<Transform>().Rotation=Quaternion::FromEuler({-Pi*.5f,0,0});
    auto first=Lighting::BeginBake(scene,settings);Wait(first);assert(first.Apply(scene));assert(probe.Baked);assert(first.GetStatus()==LightBakeStatus::Applied);auto direct=Lighting::Sample(scene,{}, {0,1,0});assert(direct.X>50);assert(Lighting::Sample(scene,{}, {0,-1,0}).X<direct.X*.1f);assert(Lighting::Sample(scene,{100,0,0},{0,1,0}).LengthSquared()==0);
    auto stale=Lighting::BeginBake(scene,settings);Wait(stale);sun.GetComponent<Transform>().Position.X=1;assert(!stale.Apply(scene));assert(stale.GetStatus()==LightBakeStatus::Failed);assert(!stale.GetError().empty());sun.GetComponent<Transform>().Position.X=0;
    auto mixed=Lighting::BeginBake(scene,settings);Wait(mixed);Scene other;Lighting::CreateProbeGrid(other,{},{},1,1,1);assert(!mixed.Apply(other));
    light.Mode=LightMode::Mixed;auto indirect=Lighting::BeginBake(scene,settings);Wait(indirect);assert(indirect.Apply(scene));assert(Lighting::Sample(scene,{},{0,1,0}).LengthSquared()==0);
    light.Mode=LightMode::Baked;auto plane=scene.CreateEntity("Occluder");plane.GetComponent<Transform>().Position.Y=2;auto& surface=plane.AddComponent<PrimitiveObject>();surface.Shape=PrimitiveShape::Plane;surface.Width=100;surface.Depth=100;
    auto excluded=Lighting::BeginBake(scene,settings);Wait(excluded);assert(excluded.Apply(scene));assert(Lighting::Sample(scene,{},{0,1,0}).X>50);surface.ContributeGI=true;
    auto blocked=Lighting::BeginBake(scene,settings);Wait(blocked);assert(blocked.Apply(scene));assert(Lighting::Sample(scene,{},{0,1,0}).X<1);
    scene.DestroyEntity(plane);light.Enabled=false;settings.AmbientRadiance={2,1,.5f};auto ambient=Lighting::BeginBake(scene,settings);Wait(ambient);assert(ambient.Apply(scene));auto value=Lighting::Sample(scene,{},{0,1,0});probe.Enabled=false;assert(Lighting::Sample(scene,{},{0,1,0}).LengthSquared()==0);probe.Enabled=true;assert(std::abs(value.X-2*Pi)<.2f);assert(std::abs(value.Y-Pi)<.1f);
    auto cancel=Lighting::BeginBake(scene,settings);cancel.Cancel();assert(cancel.GetStatus()==LightBakeStatus::Cancelled);assert(!cancel.Apply(scene));
    Runtime::Engine engine;
    {auto& registry=engine.GetSceneSerializer().GetComponents();PropertyMap data;auto legacy=other.CreateEntity("Legacy light");auto* descriptor=registry.Find("Bazzalt.Light");assert(descriptor&&descriptor->Serialize(sun,data));data.erase("Mode");assert(descriptor->Deserialize(legacy,data,1));assert(legacy.GetComponent<Light>().Mode==LightMode::Realtime);
    auto source=other.CreateEntity("Legacy primitive");source.AddComponent<PrimitiveObject>().ContributeGI=true;descriptor=registry.Find("Bazzalt.PrimitiveObject");data.clear();assert(descriptor->Serialize(source,data));data.erase("ContributeGI");assert(descriptor->Deserialize(source,data,2));assert(!source.GetComponent<PrimitiveObject>().ContributeGI);
    auto model=other.CreateEntity("Legacy mesh");model.AddComponent<Mesh>().ContributeGI=true;descriptor=registry.Find("Bazzalt.Mesh");data.clear();assert(descriptor->Serialize(model,data));data.erase("ContributeGI");assert(descriptor->Deserialize(model,data,3));assert(!model.GetComponent<Mesh>().ContributeGI);}
    auto file=std::filesystem::temp_directory_path()/("bazzalt-gi-"+UUID::Generate().ToString()+".bscene");assert(engine.SaveSceneAsset(scene,file));auto loaded=engine.LoadSceneAsset(file);assert(loaded);assert(std::abs(Lighting::Sample(*loaded,{},{0,1,0}).X-value.X)<.001f);assert(loaded->GetEntity(sun.GetUUID()).GetComponent<Light>().Mode==LightMode::Baked);std::filesystem::remove(file);
    Runtime::RenderBackend backend;assert(backend.Initialize(true));{Scene lights;auto e=lights.CreateEntity("Mode test");auto& l=e.AddComponent<Light>();auto& system=lights.AddSystem<Runtime::LightSystem>(backend);l.Mode=LightMode::Baked;TickSystem::Tick(system,lights);assert(backend.GetScene().getLightCount()==0);l.Mode=LightMode::Mixed;TickSystem::Tick(system,lights);assert(backend.GetScene().getLightCount()==1);l.Mode=LightMode::Realtime;TickSystem::Tick(system,lights);assert(backend.GetScene().getLightCount()==1);l.Enabled=false;TickSystem::Tick(system,lights);assert(backend.GetScene().getLightCount()==0);}auto handle=backend.GetAssets().CreatePrimitive(PrimitiveObject{});backend.GetAssets().UpdateBakedLighting(scene);Lighting::Clear(scene);assert(!probe.Baked);assert(Lighting::Sample(scene,{},{0,1,0}).LengthSquared()==0);backend.GetAssets().UpdateBakedLighting(scene);backend.GetAssets().DestroyPrimitive(handle);backend.Shutdown();Jobs::WaitAll();
}
