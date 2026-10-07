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
#include "GUI/GuiSystem.h"
#include "GUI/GuiRenderer.h"
#include "Runtime/Engine.h"
#include "Runtime/InputAccess.h"
#include "Rendering/RenderBackend.h"
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
    auto split=scene.CreateEntity("Split");auto& splitFrame=split.AddComponent<Frame>();splitFrame.Mode=FrameMode::CameraBound;splitFrame.Camera=camera.GetUUID();splitFrame.ScaleMode=GuiScaleMode::ConstantPixels;
    auto label=scene.CreateEntity("Label");label.SetParent(split,false);label.AddComponent<RectTransform>();label.AddComponent<GuiText>().Value="Hello";gui.Layout(scene,{400,300});assert(GUI::GetBounds(label).Position.X==200);cam.Enabled=false;gui.Layout(scene,{400,300});assert(GUI::GetBounds(label).Size.X==0);cam.Enabled=true;
    auto spatial=scene.CreateEntity("Spatial");auto& space=spatial.AddComponent<Frame>();space.Mode=FrameMode::Spatial;space.ReferenceSize={100,100};space.Camera=camera.GetUUID();camera.GetComponent<Transform>().Position={0,0,2};cam.Viewport={};
    auto surface=scene.CreateEntity("WorldButton");surface.SetParent(spatial,false);surface.AddComponent<RectTransform>().Size={100,100};surface.AddComponent<Rectangle>();surface.AddComponent<GuiButton>();Runtime::InputAccess::MotionEvent(200,150,0,0);step();assert(GUI::GetInteraction(surface).Hovered);
    Runtime::InputAccess::Shutdown();
    {Runtime::Engine persistence;auto path=std::filesystem::temp_directory_path()/std::filesystem::path("bazzalt-gui-"+UUID::Generate().ToString()+".bscene");assert(persistence.SaveSceneAsset(scene,path));auto loaded=persistence.LoadSceneAsset(path);assert(loaded);assert(loaded->GetEntity(input.GetUUID()).GetComponent<GuiTextInput>().Value=="A");assert(loaded->GetEntity(split.GetUUID()).GetComponent<Frame>().Camera==camera.GetUUID());std::filesystem::remove(path);}
    {Runtime::RenderBackend backend;assert(backend.Initialize(true));backend.SetGuiScene(&scene);Runtime::GuiRenderer renderer(backend);gui.Layout(scene,{400,300});renderer.PrepareSpatial(scene);assert(renderer.GetBatchCount()>0);space.Enabled=false;gui.Layout(scene,{400,300});renderer.PrepareSpatial(scene);assert(renderer.GetBatchCount()==0);
     auto* swap=backend.GetEngine().createSwapChain(400u,300u);renderer.PrepareOverlay(scene,{400,300});assert(backend.GetRenderer().beginFrame(swap));renderer.RenderOverlay(scene,{400,300});backend.GetRenderer().endFrame();assert(renderer.GetBatchCount()>0);backend.GetEngine().destroy(swap);backend.SetGuiScene(nullptr);}
    {Runtime::Engine engine;assert(engine.Init(true));auto c=engine.GetScene().CreateEntity("Target");c.AddComponent<Camera>();auto& target=c.AddComponent<CameraRenderTarget>();target.Width=64;target.Height=32;engine.RenderEditorFrame();auto& backend=Runtime::GuiTestAccess::Backend(engine);auto* texture=backend.GetCameraTexture(c.GetUUID());assert(texture&&texture->getWidth()==64&&texture->getHeight()==32);target.Width=128;engine.RenderEditorFrame();assert(backend.GetCameraTexture(c.GetUUID())->getWidth()==128);target.Enabled=false;engine.RenderEditorFrame();assert(!backend.GetCameraTexture(c.GetUUID()));engine.Shutdown();}
    if(std::getenv("BAZZALT_GUI_GPU_TEST")){
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
        pixels.AddSystem<Runtime::CameraSystem>(backend).Synchronize(pixels);auto* feedView=backend.GetActiveViews().front();auto* feedRenderTarget=const_cast<filament::RenderTarget*>(feedView->getRenderTarget());
        guiRenderer.PrepareOverlay(pixels,{128,128},feed.GetUUID(),feedRenderTarget);guiRenderer.PrepareOverlay(pixels,{128,128});
        assert(renderer.beginFrame(swap));renderer.setClearOptions({.clearColor={0,0,0,1},.clear=true});backend.RenderCameraView(feedView);
        guiRenderer.RenderCameraOverlay(pixels,feed.GetUUID(),{128,128},feedRenderTarget);
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
