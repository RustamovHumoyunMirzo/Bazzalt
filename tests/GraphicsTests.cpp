#include "Bazzalt/Graphics.h"
#include "Bazzalt/Components/Camera.h"
#include "Runtime/Engine.h"
#include "Runtime/GraphicsAccess.h"
#include "Rendering/GraphicsRenderer.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderSystems.h"
#include "GUI/GuiSystem.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <utils/EntityManager.h>
#include <cassert>
#include <limits>
#include <string_view>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
template<class F> bool Throws(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){
    Engine host;auto& scene=host.GetScene();GraphicsAccess::BindScene(&scene);
    auto vertex=[](Vec3 p){GraphicsVertex v;v.Position=p;v.Color={0,1,0,1};return v;};
    const std::vector<GraphicsVertex> vertices{vertex({-1,-1,-3}),vertex({1,-1,-3}),vertex({0,1,-3})};
    auto mesh=Graphics::CreateMesh(vertices);auto copy=mesh;
    assert(mesh.IsValid()&&mesh.GetVertexCount()==3&&mesh.GetIndexCount()==3);
    assert(Throws([&]{Graphics::CreateMesh(vertices,{0,1,3});}));
    assert(Throws([&]{Graphics::CreateMesh(vertices,{0,1},GraphicsTopology::Triangles);}));
    auto invalid=vertices;invalid[0].Position.X=std::numeric_limits<float>::quiet_NaN();assert(Throws([&]{Graphics::CreateMesh(invalid);}));
    Graphics::DrawMesh(mesh);auto revision=mesh.GetRevision();mesh.SetGeometry(vertices);assert(mesh.GetRevision()==revision+1);
    assert(GraphicsAccess::Commands().front().Geometry->Revision==revision);
    copy.Destroy();assert(!mesh.IsValid());assert(GraphicsAccess::Commands().front().Geometry->Vertices.size()==3);assert(Throws([&]{Graphics::DrawMesh(mesh);}));Graphics::ClearDraws();
    // Soft surfaces enter the same public Graphics pipeline with stable mesh revisions.
    auto fabric=scene.CreateEntity("Fabric");auto& cloth=fabric.AddComponent<Cloth>();cloth.SegmentsX=cloth.SegmentsY=2;
    GraphicsAccess::BeginFrame();assert(GraphicsAccess::Commands().size()==1);assert(GraphicsAccess::Commands().front().Owner==fabric.GetUUID());auto softRevision=GraphicsAccess::Commands().front().Geometry->Revision;GraphicsAccess::EndFrame();
    GraphicsAccess::BeginFrame();assert(GraphicsAccess::Commands().front().Geometry->Revision==softRevision);GraphicsAccess::EndFrame();
    fabric.SetComponentEnabled<Cloth>(false);GraphicsAccess::BeginFrame();assert(GraphicsAccess::Commands().empty());GraphicsAccess::EndFrame();scene.DestroyEntity(fabric);
    auto owner=scene.CreateEntity("Owner");int dead=0;Graphics::OnBeginFrame([&](const auto&){++dead;},owner);scene.DestroyEntity(owner);
    int scoped=0;auto scope=GraphicsAccess::SetScope(&scoped);Graphics::OnBeginFrame([&](const auto&){++scoped;});GraphicsAccess::SetScope(scope);GraphicsAccess::DisconnectScope(&scoped);
    auto subscription=Graphics::OnBeforeCamera([](const GraphicsEvent&){Graphics::DrawLine({0,0,-3},{1,0,-3});});
    GraphicsAccess::BeginFrame();assert(dead==0&&scoped==0);
    GraphicsEvent camera;camera.Camera=UUID::Generate();camera.Width=64;camera.Height=64;GraphicsAccess::BeforeCamera(camera);
    assert(GraphicsAccess::Commands().size()==1&&GraphicsAccess::Matches(GraphicsAccess::Commands()[0],camera));
    auto other=camera;other.Camera=UUID::Generate();assert(!GraphicsAccess::Matches(GraphicsAccess::Commands()[0],other));
    Graphics::Unsubscribe(subscription);GraphicsAccess::EndFrame();assert(GraphicsAccess::Commands().empty());
    // Ordered material passes: migration, validation, atomic changes and old scenes.
    auto a=Material::Create(ShaderPreset::Canvas),b=Material::Create(ShaderPreset::Canvas);assert(a.IsValid()&&b.IsValid());
    CameraPostProcessing post;post.SetMaterial(a);assert(post.AddMaterial(b,false)==1);assert(!post.MaterialAsset);
    post.MoveMaterial(1,0);assert(post.GetMaterials()[0].MaterialAsset==b.GetAssetUUID()&&!post.GetMaterials()[0].Enabled);
    post.SetMaterialEnabled(0,true);assert(post.GetMaterials()[0].Enabled);
    assert(Throws([&]{post.AddMaterial(Material::Create());}));assert(post.GetMaterials().size()==2);
    assert(Throws([&]{post.MoveMaterial(0,2);}));post.RemoveMaterial(0);assert(post.GetMaterial().GetAssetUUID()==a.GetAssetUUID());
    auto entity=scene.CreateEntity("Camera");entity.AddComponent<Camera>().PostProcessing=post;
    auto* descriptor=host.GetSceneSerializer().GetComponents().Find("Bazzalt.Camera");assert(descriptor&&descriptor->Version==4);
    PropertyMap properties;assert(descriptor->Serialize(entity,properties));auto restored=scene.CreateEntity("Restored");assert(descriptor->Deserialize(restored,properties,4));assert(restored.GetComponent<Camera>().PostProcessing.GetMaterials().size()==1);
    auto legacy=properties;legacy["Post.Material"]=b.GetAssetUUID().ToString();legacy.erase("Post.Materials.Count");auto old=scene.CreateEntity("Old");assert(descriptor->Deserialize(old,legacy,3));assert(old.GetComponent<Camera>().PostProcessing.GetMaterials()[0].MaterialAsset==b.GetAssetUUID());
    properties["Post.Materials.Count"]="33";assert(!descriptor->Deserialize(restored,properties,4));
    post.ClearMaterials();assert(post.GetMaterials().empty());
    // Exercise real buffer/material creation even on CI's NOOP backend.
    RenderBackend backend;const bool gpu=argc>1&&std::string_view(argv[1])=="--gpu";
    if(gpu)assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(!gpu));
    {
        GraphicsRenderer renderer(backend);auto& engine=backend.GetEngine();auto cameraEntity=engine.getEntityManager().create();auto* cameraObject=engine.createCamera(cameraEntity);
        cameraObject->setProjection(60.0,1.0,.1,100.0,filament::Camera::Fov::VERTICAL);auto* view=engine.createView();view->setScene(&backend.GetScene());view->setCamera(cameraObject);view->setPostProcessingEnabled(false);view->setViewport({0,0,64,64});
        auto* color=filament::Texture::Builder().width(64).height(64).levels(1).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::BLIT_SRC).build(engine);
        auto* target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,color).build(engine);view->setRenderTarget(target);
        const auto baseline=backend.GetScene().getRenderableCount();renderer.BeginFrame();renderer.AddCamera(view,entity.GetUUID(),0,false);Graphics::DrawMesh(Graphics::CreateMesh(vertices));renderer.Prepare();renderer.BeforeCamera(view,0);
        assert(backend.GetScene().getRenderableCount()==baseline+1);
        if(gpu){backend.GetRenderer().setClearOptions({.clearColor={0,0,0,1},.clear=true});auto* swap=engine.createSwapChain(1u,1u);assert(backend.GetRenderer().beginFrame(swap));backend.GetRenderer().render(view);backend.GetRenderer().endFrame();engine.destroy(swap);engine.flushAndWait();engine.pumpMessageQueues();std::vector<std::uint8_t> pixels(64*64*4);bool done=false;backend.GetRenderer().readPixels(target,0,0,64,64,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* p){*static_cast<bool*>(p)=true;},&done});engine.flushAndWait();engine.pumpMessageQueues();assert(done);auto center=(32*64+32)*4;assert(pixels[center+1]>pixels[center]+30);}
        renderer.AfterCamera(view,0);renderer.EndFrame();assert(backend.GetScene().getRenderableCount()==baseline);
        engine.destroy(view);engine.destroy(target);engine.destroy(color);engine.destroyCameraComponent(cameraEntity);engine.getEntityManager().destroy(cameraEntity);
    }
    if(gpu){
        Scene capture;capture.AddSystem<GuiSystem>();capture.AddSystem<CameraSystem>(backend);backend.SetGuiScene(&capture);
        auto entity=capture.CreateEntity("Effect camera");auto& camera=entity.AddComponent<Camera>();camera.PostProcessing.Bloom=false;camera.PostProcessing.AmbientOcclusion=false;camera.PostProcessing.AntiAliasingMode=AntiAliasing::None;camera.PostProcessing.ToneMappingMode=ToneMapping::Linear;
        TextureDescriptor descriptor;descriptor.Width=descriptor.Height=64;descriptor.Mipmaps=false;auto output=Texture::Create(descriptor);camera.SetRenderTarget(output);
        auto white=vertices;for(auto& vertex:white)vertex.Color={1,1,1,1};auto geometry=Graphics::CreateMesh(white);
        auto token=Graphics::OnBeginFrame([&](const GraphicsEvent&){Graphics::DrawMesh(geometry);});
        auto& engine=backend.GetEngine();
        auto read=[&]{backend.Render();engine.flushAndWait();engine.pumpMessageQueues();auto* target=backend.GetCameraOutputTarget(entity.GetUUID());assert(target);std::vector<std::uint8_t> pixels(64*64*4);bool done=false;backend.GetRenderer().readPixels(target,0,0,64,64,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* p){*static_cast<bool*>(p)=true;},&done});engine.flushAndWait();engine.pumpMessageQueues();assert(done);return int(pixels[(32*64+32)*4]);};
        const auto original=read();auto first=Material::Create(ShaderPreset::Canvas);first.SetColor("baseColor",{.5f,1,1,1});auto second=first.Instantiate();
        camera.PostProcessing.AddMaterial(first);const auto once=read();camera.PostProcessing.AddMaterial(second);const auto twice=read();
        assert(once<original-20&&twice<once-20);
        camera.PostProcessing.SetMaterialEnabled(0,false);assert(std::abs(read()-once)<=5);
        camera.PostProcessing.Enabled=false;assert(read()>once+20);
        camera.Enabled=false;backend.Render();assert(!backend.GetCameraTexture(entity.GetUUID()));
        Graphics::Unsubscribe(token);capture.RemoveSystem<CameraSystem>();backend.SetGuiScene(nullptr);output.Destroy();
    }
    backend.Shutdown();GraphicsAccess::BindScene(nullptr);
}
