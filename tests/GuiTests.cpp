#include <cassert>
#include <filesystem>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#include "Bazzalt/GUI.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/Serialization.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include "Bazzalt/MaterialBuilder.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include <filament/RenderTarget.h>
#include "GUI/GuiSystem.h"
#include "GUI/GuiRenderer.h"
#include "Runtime/Engine.h"
#include "Runtime/InputAccess.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include "Rendering/CaptureGraph.h"
#include "Rendering/RenderSystems.h"
#include <filament/Texture.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>
using namespace Bazzalt;
namespace Bazzalt::Runtime {struct GuiTestAccess {static RenderBackend& Backend(Engine& engine){return *engine.m_renderBackend;}};}
int main(){
    {const auto a=UUID::Generate(),b=UUID::Generate(),c=UUID::Generate();
     auto plan=Runtime::BuildCapturePlan({a,b,c},{{a,{b}},{b,{c}}});assert(plan.size()==3&&plan[0][0]==c&&plan[1][0]==b&&plan[2][0]==a);
     plan=Runtime::BuildCapturePlan({a,b,c},{{a,{b}},{b,{a,c}}});assert(plan.size()==2&&plan[0][0]==c&&plan[1].size()==2);
     plan=Runtime::BuildCapturePlan({a},{{a,{a}}});assert(plan.size()==1&&plan[0].size()==1);}
    Scene scene;auto root=scene.CreateEntity("HUD");auto& frame=root.AddComponent<Frame>();frame.ScaleMode=GuiScaleMode::ConstantPixels;
    root.AddComponent<Rectangle>(); // Never renders/hits its own Frame root.
    auto button=scene.CreateEntity("Button");assert(button.SetParent(root,false));auto& rect=button.AddComponent<RectTransform>();rect.Position={20,30};rect.Size={100,40};button.AddComponent<Rectangle>();button.AddComponent<GuiButton>();
    auto outside=scene.CreateEntity("Orphan");outside.AddComponent<Rectangle>();outside.AddComponent<RectTransform>();
    auto& gui=scene.GetSystem<Runtime::GuiSystem>();gui.Layout(scene,{400,300});assert(gui.Items.size()==1);assert(GUI::GetFrame(button)==root);assert(GUI::GetBounds(button).Position.X==20);assert(GUI::GetBounds(root).Size.X==0);assert(GUI::GetBounds(outside).Size.X==0);
    assert(Runtime::InputAccess::Initialize());Runtime::InputAccess::SetActive(true);
    const auto step=[&]{Runtime::InputAccess::BeginFrame();gui.Layout(scene,{400,300});gui.ProcessInput(scene);};
    Runtime::InputAccess::MotionEvent(30,40,0,0);step();assert(GUI::GetInteraction(button).Hovered&&GUI::GetInteraction(button).PointerEntered);assert(GUI::IsPointerOverUI(scene));
    Runtime::InputAccess::ButtonEvent(1,true);step();assert(GUI::GetInteraction(button).Pressed&&GUI::GetInteraction(button).Focused);
    Runtime::InputAccess::MotionEvent(350,250,0,0);Runtime::InputAccess::ButtonEvent(1,false);step();assert(!GUI::GetInteraction(button).Clicked);
    Runtime::InputAccess::MotionEvent(30,40,0,0);Runtime::InputAccess::ButtonEvent(1,true);Runtime::InputAccess::ButtonEvent(1,false);step();assert(GUI::GetInteraction(button).Clicked);step();assert(!GUI::GetInteraction(button).Clicked);
    auto input=scene.CreateEntity("Input");input.SetParent(root,false);input.AddComponent<RectTransform>().Position={20,90};input.AddComponent<Rectangle>();auto& text=input.AddComponent<GuiTextInput>();text.MaxLength=2;
    gui.Layout(scene,{400,300});assert(GUI::Focus(input));Runtime::InputAccess::TextEvent("A\xc3\xa9Z");step();assert(text.Value=="A\xc3\xa9"&&GUI::GetInteraction(input).ValueChanged);assert(Input::GetTextInput()=="A\xc3\xa9Z");step();assert(Input::GetTextInput().empty());
    Runtime::InputAccess::KeyEvent(static_cast<int>(KeyCode::Backspace),true);step();assert(text.Value=="A");Runtime::InputAccess::KeyEvent(static_cast<int>(KeyCode::Backspace),false);step();
    Runtime::InputAccess::KeyEvent(static_cast<int>(KeyCode::Enter),true);step();assert(GUI::GetInteraction(input).Submitted);Runtime::InputAccess::KeyEvent(static_cast<int>(KeyCode::Enter),false);step();
    text.ReadOnly=true;Runtime::InputAccess::TextEvent("B");step();assert(text.Value=="A");Runtime::InputAccess::SetActive(false);step();assert(!GUI::GetInteraction(input).Focused);assert(!GUI::IsPointerOverUI(scene));
    auto clip=scene.CreateEntity("Clip");clip.SetParent(root,false);auto& clipRect=clip.AddComponent<RectTransform>();clipRect.Position={200,0};clipRect.Size={20,20};clipRect.ClipChildren=true;
    auto child=scene.CreateEntity("Clipped");child.SetParent(clip,false);child.AddComponent<RectTransform>().Position={30,0};child.AddComponent<GuiButton>();gui.Layout(scene,{400,300});Runtime::InputAccess::SetActive(true);Runtime::InputAccess::MotionEvent(235,5,0,0);step();assert(!GUI::GetInteraction(child).Hovered);
    auto camera=scene.CreateEntity("Camera");auto& cam=camera.AddComponent<Camera>();cam.Viewport=CameraViewport::RightHalf();
    camera.AddComponent<CameraRenderTarget>();auto output=RenderTexture::FromCamera(camera);
    assert(output.IsValid(scene)&&output.Resolve(scene)==camera&&output.GetSize(scene).X==512);
    assert(output.Resize(scene,64,32)&&output.GetSize(scene).Y==32);
    bool rejectedSize=false;try{output.Resize(scene,0,32);}catch(const std::out_of_range&){rejectedSize=true;}assert(rejectedSize);
    GuiImage sourceImage;sourceImage.SetRenderTexture(output);assert(sourceImage.Camera==camera.GetUUID()&&!sourceImage.Texture);
    sourceImage.SetTexture(UUID::Generate());assert(!sourceImage.Camera&&sourceImage.Texture);
    camera.RemoveComponent<CameraRenderTarget>();assert(!output.IsValid(scene)&&output.GetSize(scene).X==0);
    auto split=scene.CreateEntity("Split");auto& splitFrame=split.AddComponent<Frame>();splitFrame.Mode=FrameMode::CameraBound;splitFrame.Camera=camera.GetUUID();splitFrame.ScaleMode=GuiScaleMode::ConstantPixels;
    auto label=scene.CreateEntity("Label");label.SetParent(split,false);label.AddComponent<RectTransform>();label.AddComponent<GuiText>().Value="Hello";gui.Layout(scene,{400,300});assert(GUI::GetBounds(label).Position.X==200);cam.Enabled=false;gui.Layout(scene,{400,300});assert(GUI::GetBounds(label).Size.X==0);cam.Enabled=true;
    auto spatial=scene.CreateEntity("Spatial");auto& space=spatial.AddComponent<Frame>();space.Mode=FrameMode::Spatial;space.ReferenceSize={100,100};space.Camera=camera.GetUUID();camera.GetComponent<Transform>().Position={0,0,2};cam.Viewport={};
    auto surface=scene.CreateEntity("WorldButton");surface.SetParent(spatial,false);surface.AddComponent<RectTransform>().Size={100,100};surface.AddComponent<Rectangle>();surface.AddComponent<GuiButton>();Runtime::InputAccess::MotionEvent(200,150,0,0);step();assert(GUI::GetInteraction(surface).Hovered);
    Runtime::InputAccess::Shutdown();
    {Runtime::Engine persistence;auto path=std::filesystem::temp_directory_path()/std::filesystem::path("bazzalt-gui-"+UUID::Generate().ToString()+".bscene");assert(persistence.SaveSceneAsset(scene,path));auto loaded=persistence.LoadSceneAsset(path);assert(loaded);assert(loaded->GetEntity(input.GetUUID()).GetComponent<GuiTextInput>().Value=="A");assert(loaded->GetEntity(split.GetUUID()).GetComponent<Frame>().Camera==camera.GetUUID());std::filesystem::remove(path);}
    {Runtime::RenderBackend backend;assert(backend.Initialize(true));backend.SetGuiScene(&scene);Runtime::GuiRenderer renderer(backend);gui.Layout(scene,{400,300});renderer.PrepareSpatial(scene);assert(renderer.GetBatchCount()>0);space.Enabled=false;gui.Layout(scene,{400,300});renderer.PrepareSpatial(scene);assert(renderer.GetBatchCount()==0);
     auto* swap=backend.GetEngine().createSwapChain(400u,300u);renderer.PrepareOverlay(scene,{400,300});assert(backend.GetRenderer().beginFrame(swap));renderer.RenderOverlay(scene,{400,300});backend.GetRenderer().endFrame();assert(renderer.GetBatchCount()>0);backend.GetEngine().destroy(swap);backend.SetGuiScene(nullptr);}
    {Runtime::Engine engine;assert(engine.Init(true));auto c=engine.GetScene().CreateEntity("Target");auto& camera=c.AddComponent<Camera>();auto& target=c.AddComponent<CameraRenderTarget>();target.Width=64;target.Height=32;
     auto output=RenderTexture::FromCamera(c);auto material=MaterialBuilder(Shader::Builtin(ShaderPreset::Unlit)).SetRenderTexture("baseColorTexture",output).Build();
     assert(material.HasOverride("baseColorTexture")&&material.GetRenderTexture("baseColorTexture").GetCameraUUID()==c.GetUUID());
     auto copy=material.Instantiate();assert(copy.GetRenderTexture("baseColorTexture").GetCameraUUID()==c.GetUUID());
     copy.SetTexture("baseColorTexture",UUID{});assert(!copy.GetRenderTexture("baseColorTexture").GetCameraUUID());
     copy.CopyPropertiesFrom(material);assert(copy.GetRenderTexture("baseColorTexture").GetCameraUUID()==c.GetUUID());
     copy.SetShader(Shader::Builtin(ShaderPreset::UnlitTransparent));assert(copy.GetRenderTexture("baseColorTexture").GetCameraUUID()==c.GetUUID());
     copy.ResetParameter("baseColorTexture");assert(!copy.HasOverride("baseColorTexture"));
     auto display=engine.GetScene().CreateEntity("Display");display.AddComponent<PrimitiveObject>().SetMaterial(material);
     engine.RenderEditorFrame();auto& backend=Runtime::GuiTestAccess::Backend(engine);auto* texture=backend.GetCameraTexture(c.GetUUID());assert(texture&&texture->getWidth()==64&&texture->getHeight()==32);
     target.Width=128;engine.RenderEditorFrame();assert(backend.GetCameraTexture(c.GetUUID())->getWidth()==128);
     camera.Active=false;engine.RenderEditorFrame();assert(!backend.GetCameraTexture(c.GetUUID())&&!output.IsValid(engine.GetScene()));
     camera.Active=true;target.Enabled=false;engine.RenderEditorFrame();assert(!backend.GetCameraTexture(c.GetUUID()));
     target.Enabled=true;engine.RenderEditorFrame();assert(backend.GetCameraTexture(c.GetUUID()));
     c.RemoveComponent<CameraRenderTarget>();engine.RenderEditorFrame();assert(!backend.GetCameraTexture(c.GetUUID()));
     engine.GetScene().DestroyEntity(c);engine.RenderEditorFrame();assert(!output.IsValid(engine.GetScene()));engine.Shutdown();}
    if(std::getenv("BAZZALT_GUI_GPU_TEST")){
        {
            Runtime::Engine host;Scene scene;Runtime::RenderBackend backend;assert(backend.Initialize(false));backend.SetGuiScene(&scene);
            TextureDescriptor description;description.Width=description.Height=64;description.Samples=4;description.Mipmaps=true;description.Filter=TextureFilter::Trilinear;description.ColorFormat=TextureColorFormat::RGBA16F;
            auto texture=Texture::Create(description);auto feed=scene.CreateEntity("Texture Producer");auto& camera=feed.AddComponent<Camera>();camera.SetRenderTarget(texture);camera.PostProcessing.Enabled=false;
            auto hud=scene.CreateEntity("Capture HUD");auto& frame=hud.AddComponent<Frame>();frame.Mode=FrameMode::CameraBound;frame.Camera=feed.GetUUID();frame.ScaleMode=GuiScaleMode::ConstantPixels;
            auto red=scene.CreateEntity("Red");red.SetParent(hud,false);red.AddComponent<RectTransform>().Size={64,64};red.AddComponent<Rectangle>().Style.Color={1,0,0,1};
            auto object=scene.CreateEntity("Monitor");auto material=MaterialBuilder(Shader::Builtin(ShaderPreset::Unlit)).SetTexture("baseColorTexture",texture).Build();object.AddComponent<PrimitiveObject>().SetMaterial(material);
            auto cameraEntity=scene.CreateEntity("Display");cameraEntity.GetComponent<Transform>().Position={0,0,3};cameraEntity.AddComponent<Camera>().PostProcessing.Enabled=false;
            auto spatial=scene.CreateEntity("Spatial feed");auto& spatialFrame=spatial.AddComponent<Frame>();spatialFrame.Mode=FrameMode::Spatial;spatialFrame.ReferenceSize={64,64};spatialFrame.PixelsPerUnit=64;
            auto image=scene.CreateEntity("Texture image");image.SetParent(spatial,false);image.AddComponent<RectTransform>().Size={64,64};image.AddComponent<GuiImage>().SetTexture(texture);
            auto& cameras=scene.AddSystem<Runtime::CameraSystem>(backend);cameras.Synchronize(scene);
            auto handle=backend.GetAssets().CreatePrimitive(object.GetComponent<PrimitiveObject>());backend.GetAssets().UpdatePrimitive(handle,object.GetWorldMatrix(),object.GetComponent<PrimitiveObject>());
            auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();auto* swap=engine.createSwapChain(64u,64u,filament::SwapChain::CONFIG_READABLE);
            auto* idle=backend.GetAssets().GetGuiTexture(texture.GetAssetUUID());assert(idle&&idle->getWidth()==64&&idle->getLevels()==7);
            backend.Render();engine.flushAndWait();assert(backend.GetCameraTexture(feed.GetUUID())->getFormat()==filament::Texture::InternalFormat::RGBA16F);
            const auto read=[&]{engine.flushAndWait();filament::View* display=nullptr;for(auto* view:backend.GetActiveViews())if(!view->getRenderTarget())display=view;assert(display);display->setViewport({0,0,64,64});std::vector<std::uint8_t> pixels(64*64*4);std::atomic<bool> ready=false;assert(renderer.beginFrame(swap));renderer.render(display);renderer.readPixels(0,0,64,64,filament::Texture::PixelBufferDescriptor(pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){static_cast<std::atomic<bool>*>(user)->store(true);},&ready));renderer.endFrame();engine.flushAndWait();assert(ready);return pixels;};
            auto pixels=read();const auto center=(32*64+32)*4;assert(pixels[center]>200&&pixels[center+1]<20); // Ordinary Texture field on a spatial image and a material.
            camera.Enabled=false;backend.Render();engine.flushAndWait();pixels=read();assert(pixels[center]<20&&pixels[center+1]<20); // No stale last frame.
            camera.Enabled=true;texture.Resize(128,128);backend.Render();engine.flushAndWait();assert(backend.GetAssets().GetGuiTexture(texture.GetAssetUUID())->getWidth()==128);
            auto duplicate=scene.CreateEntity("Conflicting writer");duplicate.AddComponent<Camera>().SetRenderTarget(texture);backend.Render();engine.flushAndWait();assert(!backend.GetCameraTexture(feed.GetUUID())&&!backend.GetCameraTexture(duplicate.GetUUID()));
            scene.DestroyEntity(duplicate);backend.Render();engine.flushAndWait();assert(backend.GetCameraTexture(feed.GetUUID()));
            camera.Active=false;backend.Render();engine.flushAndWait();pixels=read();assert(pixels[center]<20);
            camera.Active=true;backend.Render();engine.flushAndWait();assert(texture.Destroy());backend.Render();engine.flushAndWait();assert(!backend.GetCameraTexture(feed.GetUUID()));
            backend.GetAssets().DestroyPrimitive(handle);scene.RemoveSystem<Runtime::CameraSystem>();backend.SetGuiScene(nullptr);engine.destroy(swap);
        }
        {
            Runtime::Engine host;Scene graph;Runtime::RenderBackend backend;assert(backend.Initialize(false));backend.SetGuiScene(&graph);
            const auto makeFeed=[&](const char* name,int priority){auto camera=graph.CreateEntity(name);auto& settings=camera.AddComponent<Camera>();settings.Priority=priority;settings.PostProcessing.Enabled=false;auto& target=camera.AddComponent<CameraRenderTarget>();target.Width=target.Height=64;return camera;};
            auto source=makeFeed("Producer",100),middle=makeFeed("Middle",0),last=makeFeed("Consumer",-100);
            const auto widget=[&](Entity camera){auto root=graph.CreateEntity("Capture frame");auto& frame=root.AddComponent<Frame>();frame.Mode=FrameMode::CameraBound;frame.Camera=camera.GetUUID();frame.ScaleMode=GuiScaleMode::ConstantPixels;auto child=graph.CreateEntity("Capture image");child.SetParent(root,false);child.AddComponent<RectTransform>().Size={64,64};return child;};
            auto red=widget(source);red.AddComponent<Rectangle>().Style.Color={1,0,0,1};
            widget(middle).AddComponent<GuiImage>().SetRenderTexture(RenderTexture::FromCamera(source));
            widget(last).AddComponent<GuiImage>().SetRenderTexture(RenderTexture::FromCamera(middle));
            auto& cameras=graph.AddSystem<Runtime::CameraSystem>(backend);cameras.Synchronize(graph);backend.Render();
            auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();auto* swap=engine.createSwapChain(64u,64u);
            const auto read=[&](Entity camera){engine.flushAndWait();std::vector<std::uint8_t> pixels(64*64*4);std::atomic<bool> ready=false;assert(renderer.beginFrame(swap));renderer.readPixels(backend.GetCameraOutputTarget(camera.GetUUID()),0,0,64,64,filament::Texture::PixelBufferDescriptor(pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){static_cast<std::atomic<bool>*>(user)->store(true);},&ready));renderer.endFrame();engine.flushAndWait();assert(ready);return pixels;};
            auto pixels=read(last);const auto center=(32*64+32)*4;assert(pixels[center]>200&&pixels[center+1]<20); // Same-tick chain despite reversed priorities.
            auto* previous=backend.GetCameraTexture(source.GetUUID());
            red.RemoveComponent<Rectangle>();red.AddComponent<GuiImage>().SetRenderTexture(RenderTexture::FromCamera(last)); // A -> C -> B -> A.
            backend.Render();engine.flushAndWait();assert(backend.GetCameraTexture(source.GetUUID())!=previous);
            pixels=read(source);assert(pixels[center]>200); // Cycle reads its completed history.
            backend.Render();engine.flushAndWait();assert(backend.GetCameraTexture(source.GetUUID())==previous);
            red.GetComponent<GuiImage>().SetRenderTexture(RenderTexture::FromCamera(source)); // Self recursion.
            backend.Render();engine.flushAndWait();pixels=read(source);assert(pixels[center]>200);
            source.GetComponent<CameraRenderTarget>().Width=128;cameras.Synchronize(graph);backend.Render();engine.flushAndWait();
            graph.DestroyEntity(middle);cameras.Synchronize(graph);backend.Render();engine.flushAndWait();
            graph.RemoveSystem<Runtime::CameraSystem>();backend.SetGuiScene(nullptr);engine.destroy(swap);
        }
        // Actual capture + material sampling without a Game panel or platform
        // window. This catches regressions that a NOOP backend cannot detect.
        {
            Runtime::Engine materialHost; // Bind gameplay material services.
            Scene scene;Runtime::RenderBackend backend;assert(backend.Initialize(false));backend.SetGuiScene(&scene);
            auto feed=scene.CreateEntity("Feed");feed.AddComponent<Camera>().PostProcessing.Enabled=false;
            auto& target=feed.AddComponent<CameraRenderTarget>();target.Width=target.Height=64;
            auto output=RenderTexture::FromCamera(feed);
            auto hud=scene.CreateEntity("Capture HUD");auto& frame=hud.AddComponent<Frame>();frame.Mode=FrameMode::CameraBound;frame.Camera=feed.GetUUID();frame.ScaleMode=GuiScaleMode::ConstantPixels;
            auto red=scene.CreateEntity("Red");red.SetParent(hud,false);red.AddComponent<RectTransform>().Size={64,64};red.AddComponent<Rectangle>().Style.Color={1,0,0,1};
            auto object=scene.CreateEntity("Monitor");auto material=MaterialBuilder(Shader::Builtin(ShaderPreset::Unlit)).SetRenderTexture("baseColorTexture",output).Build();object.AddComponent<PrimitiveObject>().SetMaterial(material);
            auto camera=scene.CreateEntity("Display Camera");camera.GetComponent<Transform>().Position={0,0,3};auto& settings=camera.AddComponent<Camera>();settings.PostProcessing.Enabled=false;
            auto& cameras=scene.AddSystem<Runtime::CameraSystem>(backend);cameras.Synchronize(scene);
            auto handle=backend.GetAssets().CreatePrimitive(object.GetComponent<PrimitiveObject>());assert(handle);
            backend.GetAssets().UpdatePrimitive(handle,object.GetWorldMatrix(),object.GetComponent<PrimitiveObject>());
            backend.Render(); // The internal capture surface renders the feed.
            auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();engine.flushAndWait();
            filament::View* displayView=nullptr;for(auto* view:backend.GetActiveViews())if(!view->getRenderTarget())displayView=view;assert(displayView);displayView->setViewport({0,0,64,64});
            auto* swap=engine.createSwapChain(64u,64u,filament::SwapChain::CONFIG_READABLE);
            std::vector<std::uint8_t> pixels(64*64*4);std::atomic<bool> ready=false;
            assert(renderer.beginFrame(swap));renderer.render(displayView);
            renderer.readPixels(0,0,64,64,filament::Texture::PixelBufferDescriptor(pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* data){static_cast<std::atomic<bool>*>(data)->store(true);},&ready));renderer.endFrame();engine.flushAndWait();assert(ready);
            const auto center=(32*64+32)*4;assert(pixels[center]>200&&pixels[center+1]<20&&pixels[center+2]<20);
            // Resizing must detach material samplers before retiring textures.
            assert(output.Resize(scene,128,128));cameras.Synchronize(scene);backend.Render();engine.flushAndWait();
            feed.GetComponent<Camera>().Enabled=false;cameras.Synchronize(scene);backend.Render();engine.flushAndWait();
            backend.GetAssets().DestroyPrimitive(handle);scene.RemoveSystem<Runtime::CameraSystem>();backend.SetGuiScene(nullptr);engine.destroy(swap);
        }
        Scene pixels;auto hud=pixels.CreateEntity("Pixel HUD");hud.AddComponent<Frame>().ScaleMode=GuiScaleMode::ConstantPixels;
        auto box=pixels.CreateEntity("Pixel Box");box.SetParent(hud,false);auto& layout=box.AddComponent<RectTransform>();layout.Position={20,30};layout.Size={40,20};box.AddComponent<Rectangle>().Style.Color={1,0,0,1};
        auto clipping=pixels.CreateEntity("Pixel Clip");clipping.SetParent(hud,false);auto& container=clipping.AddComponent<RectTransform>();container.Position={80,30};container.Size={10,10};container.ClipChildren=true;
        auto green=pixels.CreateEntity("Clipped Green");green.SetParent(clipping,false);green.AddComponent<RectTransform>().Size={40,20};green.AddComponent<Rectangle>().Style.Color={0,1,0,1};
        auto letters=pixels.CreateEntity("Pixel Text");letters.SetParent(hud,false);letters.AddComponent<RectTransform>().Position={20,70};auto& glyph=letters.AddComponent<GuiText>();glyph.Value="A";glyph.Color={0,0,1,1};
        Runtime::RenderBackend backend;assert(backend.Initialize(false));backend.SetGuiScene(&pixels);Runtime::GuiRenderer guiRenderer(backend);
        auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();auto* swap=engine.createSwapChain(128u,128u,filament::SwapChain::CONFIG_READABLE);
        auto cameraEntity=engine.getEntityManager().create();auto* clearCamera=engine.createCamera(cameraEntity);auto* clearView=engine.createView();clearView->setCamera(clearCamera);clearView->setViewport({0,0,128,128});
        for(int frameIndex=0;frameIndex<3;++frameIndex){guiRenderer.PrepareOverlay(pixels,{128,128});assert(renderer.beginFrame(swap));renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});renderer.render(clearView);guiRenderer.RenderOverlay(pixels,{128,128});renderer.endFrame();engine.flushAndWait();}
        auto feed=pixels.CreateEntity("GUI Feed");feed.AddComponent<Camera>();auto& feedTarget=feed.AddComponent<CameraRenderTarget>();feedTarget.Width=feedTarget.Height=128;
        auto& feedFrame=hud.GetComponent<Frame>();feedFrame.Mode=FrameMode::CameraBound;feedFrame.Camera=feed.GetUUID();
        auto screen=pixels.CreateEntity("Feed Display");screen.AddComponent<Frame>().ScaleMode=GuiScaleMode::ConstantPixels;
        auto image=pixels.CreateEntity("Camera Image");image.SetParent(screen,false);image.AddComponent<RectTransform>().Size={128,128};image.AddComponent<GuiImage>().Camera=feed.GetUUID();
        pixels.AddSystem<Runtime::CameraSystem>(backend).Synchronize(pixels);backend.Render();auto* feedRenderTarget=backend.GetCameraOutputTarget(feed.GetUUID());assert(feedRenderTarget);
        guiRenderer.PrepareOverlay(pixels,{128,128});
        assert(renderer.beginFrame(swap));renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});
        std::vector<std::uint8_t> sourceBuffer(128*128*4);std::atomic<bool> sourceReady=false;
        renderer.readPixels(feedRenderTarget,0,0,128,128,filament::Texture::PixelBufferDescriptor(sourceBuffer.data(),sourceBuffer.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){static_cast<std::atomic<bool>*>(user)->store(true);},&sourceReady));
        renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});renderer.render(clearView);guiRenderer.RenderOverlay(pixels,{128,128});
        std::vector<std::uint8_t> buffer(128*128*4);std::atomic<bool> ready=false;
        renderer.readPixels(0,0,128,128,filament::Texture::PixelBufferDescriptor(buffer.data(),buffer.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,std::size_t,void* user){static_cast<std::atomic<bool>*>(user)->store(true);},&ready));
        renderer.endFrame();for(int i=0;i<50&&!ready;++i){engine.flushAndWait();std::this_thread::sleep_for(std::chrono::milliseconds(10));}assert(ready);
        assert(sourceReady);std::printf("GUI source background %u,%u,%u,%u, center %u,%u\n",sourceBuffer[(5*128+5)*4],sourceBuffer[(5*128+5)*4+1],sourceBuffer[(5*128+5)*4+2],sourceBuffer[(5*128+5)*4+3],sourceBuffer[(40*128+40)*4],sourceBuffer[(40*128+40)*4+1]);
        const auto pixel=[&](int x,int y,int channel){return buffer[(y*128+x)*4+channel];};
        int maxR=0,maxG=0,greenPixels=0,bluePixels=0;for(int y=0;y<128;++y)for(int x=0;x<128;++x){maxR=std::max(maxR,int(pixel(x,y,0)));maxG=std::max(maxG,int(pixel(x,y,1)));if(pixel(x,y,1)>200)++greenPixels;if(pixel(x,y,2)>100)++bluePixels;}
        int count=0,minY=128,maxY=0,minX=128,maxX=0;for(int y=0;y<128;++y)for(int x=0;x<128;++x)if(pixel(x,y,0)>100){++count;minY=std::min(minY,y);maxY=std::max(maxY,y);minX=std::min(minX,x);maxX=std::max(maxX,x);}
        std::printf("GUI red pixels: %d, bounds %d,%d to %d,%d; center %u,%u; flipped %u,%u\n",count,minX,minY,maxX,maxY,pixel(40,88,0),pixel(40,88,1),pixel(40,40,0),pixel(40,40,1));std::fflush(stdout);
        // Filament's readback exposes backend row orientation; assert the
        // exact rectangle extent and accept either top/bottom row ordering.
        assert(count==800&&minX==20&&maxX==59);
        assert((minY==30&&maxY==49)||(minY==78&&maxY==97));
        assert(maxR>200&&maxG>200&&greenPixels==100&&bluePixels>0);assert(pixel(5,5,0)<20&&pixel(5,5,1)<20);
        guiRenderer.Clear();pixels.RemoveSystem<Runtime::CameraSystem>();backend.SetGuiScene(nullptr);engine.destroy(clearView);engine.destroyCameraComponent(cameraEntity);engine.getEntityManager().destroy(cameraEntity);engine.destroy(swap);
    }
}
