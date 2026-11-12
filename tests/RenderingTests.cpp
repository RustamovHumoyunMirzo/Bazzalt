#include <filament/Skybox.h>
#include "Bazzalt/Components/LocalFog.h"
#include "Rendering/RenderBackend.h"
#include <cassert>
#include <filament/Renderer.h>
#include "Runtime/TimeAccess.h"

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Bazzalt/Terrain.h"
#include "Runtime/Engine.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/LightGuides.h"

namespace Bazzalt::Runtime {
struct RenderFailureTestAccess {
    static void Check(){
        RenderBackend backend;assert(backend.Initialize(true));
        assert(backend.CreateViewport(900,0,RenderBackend::ViewportKind::Scene,64,64,1,true));
        backend.m_gpuWaitFailed=true;
        assert(!backend.WaitForIdle());
        assert(!backend.CreateViewport(901,0,RenderBackend::ViewportKind::Game,64,64,1,true));
        backend.DestroyViewport(900);
        assert(backend.GetStatistics().at("viewports")==1);
        bool rejected=false;try{backend.Render();}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
        // This test injects a failure state, not an actual device failure.
        backend.m_gpuWaitFailed=false;backend.DestroyViewport(900);
        assert(backend.GetStatistics().at("viewports")==0);backend.Shutdown();
    }
};
struct EditorOutlineTestAccess {
    static void Check(Engine& engine,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered){
        engine.UpdateEditorOverlays();
        assert(engine.m_renderBackend->m_outlineSelected==selected);
        assert(engine.m_renderBackend->m_outlineHovered==hovered);
    }
};
}
#include "Rendering/LightParameters.h"
#include "Rendering/FogRenderer.h"
#include <filament/SwapChain.h>
#include "Rendering/RenderSystems.h"
#include "Rendering/AntiAliasingSettings.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include <filament/Scene.h>
#include <filament/Engine.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/LightManager.h>
#include <filament/Camera.h>
#include <filament/Exposure.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <iostream>
#include <cmath>
#include <string_view>
#include <limits>

struct SystemTestAccess : Bazzalt::System {
    static void Tick(Bazzalt::System& system,Bazzalt::Scene& scene){
        constexpr auto update=&SystemTestAccess::OnUpdate;(system.*update)(scene,0);
    }
};

// Manual real-GPU regression; NOOP tests cannot prove a post effect changes pixels.
int CheckDofPixels(){
    using namespace Bazzalt;using namespace Bazzalt::Runtime;
    RenderBackend backend;assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(false));backend.SetPresentationSize(320,240);
    {
        Scene scene;auto& cameras=scene.AddSystem<CameraSystem>(backend);auto& lights=scene.AddSystem<LightSystem>(backend);
        auto cameraEntity=scene.CreateEntity("Pixel-test camera");auto& camera=cameraEntity.AddComponent<Camera>();
        camera.PostProcessing.Bloom=false;camera.PostProcessing.AmbientOcclusion=false;camera.PostProcessing.AntiAliasingMode=AntiAliasing::None;
        camera.PostProcessing.DepthOfField.FocusDistance=1;camera.PostProcessing.DepthOfField.Aperture=.5f;
        scene.CreateEntity("Pixel-test sun").AddComponent<Light>().Intensity=100000;
        SystemTestAccess::Tick(lights,scene);
        std::vector<RenderAssets::Handle> objects;
        for(int y=-3;y<=3;++y)for(int x=-4;x<=4;++x){PrimitiveObject object;object.Color={(x+y)%2?1.f:.05f,.3f,.1f,1};auto handle=backend.GetAssets().CreatePrimitive(object);objects.push_back(handle);backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({x*.45f,y*.45f,-5.f})*Mat4::Scaling({.35f,.35f,.35f}),object);}
        auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();
        auto* color=filament::Texture::Builder().width(320).height(240).levels(1).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::BLIT_SRC|filament::Texture::Usage::SAMPLEABLE).build(engine);
        auto* target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,color).build(engine);
        const auto capture=[&](bool enabled){
            camera.PostProcessing.DepthOfField.Enabled=enabled;SystemTestAccess::Tick(cameras,scene);
            auto* view=backend.GetActiveViews().front();view->setRenderTarget(target);
            for(int frame=0;frame<3;++frame){renderer.renderStandaloneView(view);engine.flushAndWait();engine.pumpMessageQueues();}
            std::vector<std::uint8_t> pixels(320*240*4);bool done=false;
            renderer.readPixels(target,0,0,320,240,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,size_t,void* user){*static_cast<bool*>(user)=true;},&done});
            engine.flushAndWait();engine.pumpMessageQueues();assert(done);return pixels;
        };
        const auto sharp=capture(false),blurred=capture(true),sharpAgain=capture(false);
        camera.PostProcessing.AntiAliasingMode=AntiAliasing::MSAA4x;const auto multisampled=capture(false);
        std::size_t aaChanged=0;for(std::size_t i=0;i<sharp.size();i+=4){int delta=0;for(int c=0;c<3;++c)delta=std::max(delta,std::abs(int(sharp[i+c])-int(multisampled[i+c])));if(delta>3)++aaChanged;}
        std::cout<<"MSAA4x changed geometry edge pixels="<<aaChanged<<std::endl;assert(aaChanged>50);
        camera.PostProcessing.AntiAliasingMode=AntiAliasing::None;
        std::size_t changed=0,restored=0;int maximum=0;
        for(std::size_t i=0;i<sharp.size();i+=4){int delta=0,restoreDelta=0;for(int channel=0;channel<3;++channel){delta=std::max(delta,std::abs(int(sharp[i+channel])-int(blurred[i+channel])));restoreDelta=std::max(restoreDelta,std::abs(int(sharp[i+channel])-int(sharpAgain[i+channel])));}if(restoreDelta>3)++restored;if(delta>3)++changed;maximum=std::max(maximum,delta);}
        std::cout<<"DoF changed pixels="<<changed<<" max difference="<<maximum<<" restored differences="<<restored<<std::endl;
        assert(changed>320*240/100);assert(restored<320*240/100);
        cameraEntity.AddComponent<GaussianBlur>().Size=1.f;
        const auto withBlur=capture(true);
        std::size_t conflictDifferences=0;
        for(std::size_t i=0;i<blurred.size();i+=4){int delta=0;for(int channel=0;channel<3;++channel)delta=std::max(delta,std::abs(int(blurred[i+channel])-int(withBlur[i+channel])));if(delta>3)++conflictDifferences;}
        std::cout<<"DoF with enabled blur conflict differences="<<conflictDifferences<<std::endl;
        assert(conflictDifferences<320*240/100);
        backend.GetActiveViews().front()->setRenderTarget(nullptr);engine.destroy(target);engine.destroy(color);
        for(auto handle:objects)backend.GetAssets().DestroyPrimitive(handle);
    }backend.Shutdown();return 0;
}

int CheckFogPixels(){
    using namespace Bazzalt;using namespace Bazzalt::Runtime;
    RenderBackend backend;assert(backend.ConfigureBackend("vulkan"));assert(backend.Initialize(false));backend.SetPresentationSize(320,240);
    {
        Scene scene;auto& cameras=scene.AddSystem<CameraSystem>(backend);
        auto entity=scene.CreateEntity("Fog camera");auto& camera=entity.AddComponent<Camera>();camera.PostProcessing.Enabled=false;
        SystemTestAccess::Tick(cameras,scene);auto* view=backend.GetActiveViews().front();
        PrimitiveObject object;auto handle=backend.GetAssets().CreatePrimitive(object);
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-5})*Mat4::Scaling({3,3,1}),object);
        auto& engine=backend.GetEngine();auto& renderer=backend.GetRenderer();
        auto* color=filament::Texture::Builder().width(320).height(240).levels(1).format(filament::Texture::InternalFormat::RGBA8).usage(filament::Texture::Usage::COLOR_ATTACHMENT|filament::Texture::Usage::BLIT_SRC|filament::Texture::Usage::SAMPLEABLE).build(engine);
        auto* target=filament::RenderTarget::Builder().texture(filament::RenderTarget::AttachmentPoint::COLOR,color).build(engine);
        auto* swap=engine.createSwapChain(320u,240u);
        view->setRenderTarget(target);view->setViewport({0,0,320,240});
        FogRenderer fogRenderer(engine,renderer);fogRenderer.Prepare({view});
        FogSettings fog;fog.Mode=FogMode::Volumetric;fog.Color={1,0,0};fog.Density=.5f;fog.StartDistance=0;fog.HeightFalloff=0;fog.NoiseAmount=0;
        const auto capture=[&](bool enabled){
            for(unsigned frame=0;frame<3;++frame){if(renderer.beginFrame(swap)){if(enabled)fogRenderer.Render(*view,fog,&scene,0);else renderer.render(view);renderer.endFrame();}engine.flushAndWait();engine.pumpMessageQueues();}
            std::vector<std::uint8_t> pixels(320*240*4);bool done=false;
            renderer.readPixels(target,0,0,320,240,{pixels.data(),pixels.size(),filament::Texture::Format::RGBA,filament::Texture::Type::UBYTE,[](void*,size_t,void* user){*static_cast<bool*>(user)=true;},&done});
            engine.flushAndWait();engine.pumpMessageQueues();assert(done);assert(view->getRenderTarget()==target);return pixels;
        };
        auto baseline=capture(false),volume=capture(true),restored=capture(false);
        auto center=(120*320+160)*4;auto corner=(10*320+10)*4;
        std::cout<<"Fog red baseline="<<int(baseline[center])<<" fog="<<int(volume[center])<<" sky="<<int(volume[corner])<<std::endl;
        assert(volume[center]>baseline[center]+40);assert(volume[center]>volume[center+1]+40);
        assert(std::abs(int(volume[corner])-int(baseline[corner]))<4);
        assert(std::abs(int(restored[center])-int(baseline[center]))<4);
        view->setPostProcessingEnabled(true);
        const auto graded=capture(false),gradedFog=capture(true);
        assert(gradedFog[center]>graded[center]+40);
        std::cout<<"Post-processed sky baseline="<<int(graded[corner])<<" fog="<<int(gradedFog[corner])<<std::endl;
        assert(std::abs(int(gradedFog[corner])-int(graded[corner]))<4);
        // Post-tonemap fog must not add raw sun radiance and clip a whole frame.
        auto sunEntity=scene.CreateEntity("Fog bright sun");
        auto& sun=sunEntity.AddComponent<Light>();sun.Type=LightType::Directional;sun.Intensity=100000;
        fog.Color={.55f,.62f,.7f};fog.Anisotropy=0;fog.AffectSky=true;
        const auto sunFog=capture(true);
        for(auto pixel:{center,corner})for(int channel=0;channel<3;++channel)
            assert(sunFog[pixel+channel]<250);
        sun.Enabled=false;
        const auto noSunFog=capture(true);
        assert(sunFog[corner]>=noSunFog[corner]);
        // Zero maximum opacity is an exact bypass even with an intense sun.
        sun.Enabled=true;fog.MaximumOpacity=0;
        const auto bypass=capture(true),unfogged=capture(false);
        for(int channel=0;channel<3;++channel)
            assert(std::abs(int(bypass[center+channel])-int(unfogged[center+channel]))<4);
        fog.MaximumOpacity=1;fog.Color={1,0,0};fog.AffectSky=false;sun.Enabled=false;
        // Distance must increase fog, while height falloff is explicitly ground-biased.
        fog.Density=.02f;fog.MaxDistance=200;fog.HeightFalloff=0;
        const auto nearFog=capture(true);
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-50})*Mat4::Scaling({30,30,1}),object);
        const auto farFog=capture(true);
        std::cout<<"Fog near="<<int(nearFog[center])<<" far="<<int(farFog[center])<<std::endl;
        assert(farFog[center]>nearFog[center]+80);
        assert(std::abs(int(farFog[center])-int(255*(1-std::exp(-.02f*49.5f))))<3);
        auto* renderCamera=const_cast<filament::Camera*>(&view->getCamera());
        const auto savedProjection=renderCamera->getProjectionMatrix(),savedCulling=renderCamera->getCullingProjectionMatrix();
        const auto savedNear=renderCamera->getNear(),savedFar=renderCamera->getCullingFar();
        renderCamera->setProjection(filament::Camera::Projection::ORTHO,-20,20,-15,15,.1,1000);
        const auto orthoFog=capture(true);
        assert(std::abs(int(orthoFog[center])-int(255*(1-std::exp(-.02f*49.4f))))<3);
        renderCamera->setCustomProjection(savedProjection,savedCulling,savedNear,savedFar);
        fog.AffectSky=true;
        const auto uniformFog=capture(true);
        const auto top=(20*320+10)*4,bottom=(220*320+10)*4;
        assert(std::abs(int(uniformFog[top])-int(uniformFog[bottom]))<4);
        fog.HeightFalloff=.2f;
        const auto heightFog=capture(true);
        std::cout<<"Height fog top="<<int(heightFog[top])<<" bottom="<<int(heightFog[bottom])<<std::endl;
        assert(heightFog[bottom]>heightFog[top]+80);
        const auto savedModel=renderCamera->getModelMatrix();
        renderCamera->lookAt({0,0,0},{0,0,-1},{0,-1,0});
        const auto rolledHeightFog=capture(true);
        assert(rolledHeightFog[top]>rolledHeightFog[bottom]+80);
        renderCamera->setModelMatrix(savedModel);
        fog.Density=.5f;fog.HeightFalloff=0;fog.AffectSky=false;
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-5})*Mat4::Scaling({3,3,1}),object);
        // Start distance fades the medium, not the light, and partial slices
        // must remain exact even when the surface crosses the first fog boundary.
        fog.StartDistance=10;fog.Density=.1f;fog.MaxDistance=100;fog.Steps=64;
        int previous=-1;
        for(float distance : {9.f,10.f,10.25f,11.f,12.5f,15.f,20.f}) {
            backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-distance-.5f})*Mat4::Scaling({30,30,1}),object);
            const auto pixels=capture(true);
            const float t=std::clamp((distance-10.f)/5.f,0.f,1.f);
            const float path=5.f*(t*t*t-.5f*t*t*t*t)+std::max(0.f,distance-15.f);
            const int expected=int(255*(1-std::exp(-.1f*path)));
            assert(std::abs(int(pixels[center])-expected)<4);
            assert(int(pixels[center])>=previous);previous=pixels[center];
        }
        // An isotropic ambient medium has the same extinction through the
        // same world distance when the camera turns away from the sun.
        fog.StartDistance=0;fog.Density=.02f;fog.Anisotropy=0;
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-50})*Mat4::Scaling({30,30,1}),object);
        const auto forwardFog=capture(true);
        renderCamera->lookAt({0,0,0},{0,0,1},{0,1,0});
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,50})*Mat4::Scaling({30,30,1}),object);
        const auto backwardFog=capture(true);
        assert(std::abs(int(forwardFog[center])-int(backwardFog[center]))<4);
        renderCamera->setModelMatrix(savedModel);
        fog.StartDistance=0;fog.Density=.5f;fog.Steps=32;
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-5})*Mat4::Scaling({3,3,1}),object);
        // Vulkan readback is top-down while viewport coordinates start at bottom-left.
        view->setViewport({0,0,160,120});fogRenderer.Prepare({view});
        const auto resized=capture(true);assert(resized[(180*320+80)*4]>40);
        fogRenderer.Prepare({});fogRenderer.Prepare({view});
        const auto recreated=capture(true);assert(recreated[(180*320+80)*4]>40);
        // A local fog box stays at a fixed world position as the camera moves.
        view->setViewport({0,0,320,240});view->setPostProcessingEnabled(false);
        fog.Enabled=false;
        // Use an explicit black surround so the direct and offscreen captures
        // have the same background, as in the engine presentation path.
        auto* localSky=filament::Skybox::Builder().color({0,0,0,1}).build(engine);
        view->getScene()->setSkybox(localSky);
        auto localEntity=scene.CreateEntity("Local fog regression");auto& local=localEntity.AddComponent<LocalFog>();
        local.Size={8,8,4};local.BlendDistance=0;local.Fog.Color={1,0,0};local.Fog.Density=.2f;local.Fog.Steps=128;
        localEntity.GetComponent<Transform>().Position={0,0,-10};
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-50})*Mat4::Scaling({100,100,1}),object);
        fogRenderer.Prepare({view},0,{},&scene);
        const auto localBaseline=capture(false);
        const auto localFront=capture(true);
        std::cout<<"Local fog front="<<int(localFront[center])<<" baseline="<<int(localBaseline[center])<<std::endl;
        assert(std::abs(int(localFront[center])-int(localBaseline[center]*std::exp(-.2f*4)+255*(1-std::exp(-.2f*4))))<5);
        local.Fog.MaximumOpacity=.25f;
        const auto cappedLocal=capture(true);assert(std::abs(int(cappedLocal[center])-int(255*.25f+localBaseline[center]*.75f))<5);
        local.Fog.MaximumOpacity=1;local.BlendDistance=1;
        const auto softLocal=capture(true);assert(softLocal[center]<localFront[center]-10);
        local.BlendDistance=0;
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-5})*Mat4::Scaling({100,100,1}),object);
        const auto foregroundBaseline=capture(false),foregroundLocal=capture(true);assert(std::abs(int(foregroundLocal[center])-int(foregroundBaseline[center]))<4);
        backend.GetAssets().UpdatePrimitive(handle,Mat4::Translation({0,0,-50})*Mat4::Scaling({100,100,1}),object);
        renderCamera->lookAt({0,0,-10},{0,0,-11},{0,1,0});
        const auto localInside=capture(true);
        assert(localInside[center]<localFront[center]-20);
        renderCamera->lookAt({0,0,-20},{0,0,-21},{0,1,0});
        const auto behindBaseline=capture(false),localBehind=capture(true);assert(std::abs(int(localBehind[center])-int(behindBaseline[center]))<4);
        renderCamera->setModelMatrix(savedModel);
        localEntity.GetComponent<Transform>().Position={30,0,-10};
        const auto shifted=capture(true);assert(std::abs(int(shifted[center])-int(localBaseline[center]))<4);
        localEntity.GetComponent<Transform>().Position={0,0,-10};
        localEntity.GetComponent<Transform>().Rotation=Quaternion::FromAxisAngle({0,1,0},Pi*.5f);
        const auto rotatedLocal=capture(true);assert(rotatedLocal[center]>localFront[center]+30);
        localEntity.GetComponent<Transform>().Rotation={};localEntity.GetComponent<Transform>().Scale={1,1,2};
        const auto scaledLocal=capture(true);assert(std::abs(int(scaledLocal[center])-int(rotatedLocal[center]))<5);
        local.Enabled=false;fogRenderer.Prepare({view},0,{},&scene);
        const auto disabledLocal=capture(true);assert(std::abs(int(disabledLocal[center])-int(localBaseline[center]))<4);
        local.Enabled=true;localEntity.GetComponent<Transform>().Scale={1,1,1};
        auto overlapEntity=scene.CreateEntity("Overlap fog");overlapEntity.AddComponent<LocalFog>()=local;
        overlapEntity.GetComponent<Transform>().Position={0,0,-10};
        fogRenderer.Prepare({view},0,{},&scene);const auto overlap=capture(true);
        assert(overlap[center]>localFront[center]+30);
        // Adding an unrelated local region must preserve scene-wide extinction.
        localEntity.GetComponent<Transform>().Position={30,0,-10};
        overlapEntity.GetComponent<Transform>().Position={30,0,-10};
        fog.Enabled=true;fog.Density=.02f;fog.MaxDistance=100;fog.Steps=128;
        fogRenderer.Prepare({view},0,{},&scene);const auto mixedGlobal=capture(true);
        local.Enabled=false;overlapEntity.GetComponent<LocalFog>().Enabled=false;
        fogRenderer.Prepare({view},0,{},&scene);const auto onlyGlobal=capture(true);
        assert(std::abs(int(mixedGlobal[center])-int(onlyGlobal[center]))<4);
        view->getScene()->setSkybox(nullptr);engine.destroy(localSky);
        view->setRenderTarget(nullptr);engine.destroy(target);engine.destroy(color);engine.destroy(swap);backend.GetAssets().DestroyPrimitive(handle);
    }
    backend.Shutdown();return 0;
}

int main(int argc,char** argv) {
    if(argc>1&&std::string_view(argv[1])=="--gpu-fog")return CheckFogPixels();
    if(argc>1&&std::string_view(argv[1])=="--gpu-dof")return CheckDofPixels();
    Bazzalt::Runtime::RenderFailureTestAccess::Check();
    {
        using namespace Bazzalt;
        Light light;light.Range=7;
        const auto point=Runtime::BuildLightGuides(light,Mat4::Identity());assert(point.size()==192);
        for(const auto& line:point){assert(std::abs(Vec3{line.AX,line.AY,line.AZ}.Length()-7)<1e-4f);assert(std::abs(Vec3{line.BX,line.BY,line.BZ}.Length()-7)<1e-4f);}
        light.Range=14;const auto larger=Runtime::BuildLightGuides(light,Mat4::Identity());assert(larger.size()==point.size());assert(larger[0].AX==point[0].AX*2);
        light.Type=LightType::Spot;light.Range=7;light.OuterConeAngle=ToRadians(89.9f);
        for(const auto& line:Runtime::BuildLightGuides(light,Mat4::Identity())){
            assert(std::isfinite(line.AX)&&std::isfinite(line.BZ));assert((Vec3{line.AX,line.AY,line.AZ}.Length()<=7.001f));assert((Vec3{line.BX,line.BY,line.BZ}.Length()<=7.001f));
        }
        light.InnerConeAngle=light.OuterConeAngle;assert(Runtime::BuildLightGuides(light,Mat4::Identity()).size()==132);
        light.OuterConeAngle=std::numeric_limits<float>::quiet_NaN();light.Range=-1;light.SunHaloSize=0;
        const auto safe=Runtime::SanitizeLight(light);assert(safe.Range>0&&std::isfinite(safe.OuterConeAngle)&&safe.InnerConeAngle<=safe.OuterConeAngle&&safe.SunHaloSize>=1);
        for(const auto& line:Runtime::BuildLightGuides(light,Mat4::Identity()))assert(std::isfinite(line.AX)&&std::isfinite(line.BZ));
        light.Enabled=false;assert(Runtime::BuildLightGuides(light,Mat4::Identity()).empty());
    }
    for(const auto shape:{Bazzalt::PrimitiveShape::Cylinder,Bazzalt::PrimitiveShape::Cone}){
        Bazzalt::PrimitiveObject primitive;primitive.Shape=shape;
        const auto geometry=Bazzalt::Runtime::BuildPrimitiveGeometry(primitive);int caps=0;
        for(std::size_t index=0;index+2<geometry.Indices.size();index+=3){
            Bazzalt::Vec3 points[3];for(int i=0;i<3;++i){const auto& p=geometry.Vertices[geometry.Indices[index+i]].Position;points[i]={p[0],p[1],p[2]};}
            const auto normal=Bazzalt::Vec3::Cross(points[1]-points[0],points[2]-points[0]);
            if(normal.LengthSquared()>1e-10f&&std::abs(points[0].Y-points[1].Y)<1e-6f&&std::abs(points[1].Y-points[2].Y)<1e-6f){assert(normal.Y*points[0].Y>0);++caps;}
        }
        assert(caps>0);
    }
    using Bazzalt::CameraViewport;
    constexpr auto left = CameraViewport::LeftHalf();
    constexpr auto right = CameraViewport::RightHalf();
    constexpr auto topRight = CameraViewport::Grid(1, 1, 2, 2);
    constexpr auto invalid = CameraViewport::Grid(2, 0, 2, 2);
    static_assert(left.X == 0.0f && left.Width == 0.5f);
    static_assert(right.X == 0.5f && right.Width == 0.5f);
    static_assert(topRight.X == 0.5f && topRight.Y == 0.5f);
    static_assert(invalid.Width == 0.0f && invalid.Height == 0.0f);

    Bazzalt::PostProcessingStack customStack;
    auto& customEffect = customStack.AddEffect(Bazzalt::UUID::Generate(), "Color curve");
    customEffect.SetParameter(Bazzalt::PostProcessParameter::Float("strength", 0.75f));
    customEffect.SetParameter(Bazzalt::PostProcessParameter::Float3(
        "tint", {1.0f, 0.8f, 0.7f}));
    assert(customStack.GetEffects().size() == 1);
    assert(customEffect.Parameters.size() == 2);

    Bazzalt::Runtime::Engine engine;
    assert(engine.Init(true));
    Bazzalt::Runtime::TimeAccess::Reset();Bazzalt::Runtime::TimeAccess::Advance(.2);engine.RenderEditorFrame();
    const auto firstMaterialTime=engine.GetEditorStatistics().at("material_time");assert(std::abs(firstMaterialTime-.2)<1e-6);
    Bazzalt::Time::SetTimeScale(.5f);Bazzalt::Runtime::TimeAccess::Advance(.2);engine.RenderEditorFrame();assert(std::abs(engine.GetEditorStatistics().at("material_time")-.3)<1e-6);
    Bazzalt::Time::SetTimeScale(0);Bazzalt::Runtime::TimeAccess::Advance(.2);engine.RenderEditorFrame();assert(std::abs(engine.GetEditorStatistics().at("material_time")-.3)<1e-6);
    Bazzalt::Runtime::TimeAccess::Reset();
#ifdef _WIN32
    // Synthetic offscreen IDs must be rejected before reaching Vulkan/OpenGL.
    assert(!engine.CreateEditorViewport(1, 0, true, 64, 64));
    assert(!engine.CreateEditorViewport(1, 1, true, 64, 64));
#endif

    auto& scene = engine.GetScene();
    auto camera = scene.CreateEntity("Camera");
    camera.GetComponent<Bazzalt::Transform>().Position = {0.0f, 0.0f, 5.0f};
    camera.AddComponent<Bazzalt::Camera>().Viewport = left;
    auto& cameraComponent = camera.GetComponent<Bazzalt::Camera>();
    cameraComponent.PostProcessing.DepthOfField.Enabled = true;
    cameraComponent.PostProcessing.DepthOfField.FocusDistance = 5.0f;
    cameraComponent.PostProcessing.CustomEffects = std::move(customStack);
    camera.AddComponent<Bazzalt::GaussianBlur>().Size = 2.0f;
    camera.AddComponent<Bazzalt::Vignette>().Intensity = 0.5f;

    auto secondCamera = scene.CreateEntity("Second Camera");
    auto& second = secondCamera.AddComponent<Bazzalt::Camera>();
    second.Viewport = right;
    second.Priority = 1;

    auto light = scene.CreateEntity("Sun");
    auto& lightComponent = light.AddComponent<Bazzalt::Light>();
    lightComponent.Type = Bazzalt::LightType::Sun;
    lightComponent.Intensity = 100000.0f;

    auto mesh = scene.CreateEntity("Mesh placeholder");
    mesh.AddComponent<Bazzalt::Mesh>();

    engine.Update();
    auto primitive=scene.CreateEntity("Pick target");primitive.AddComponent<Bazzalt::PrimitiveObject>();
    assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1})==primitive.GetUUID());
    Bazzalt::Vec3 placement;
    assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1},&placement)==primitive.GetUUID());
    assert(std::abs(placement.Z-.5f)<.001f);
    engine.SetEditorEntityState({}, {primitive.GetUUID()});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1}).IsRoot());
    engine.SetEditorEntityState({primitive.GetUUID()}, {});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1}).IsRoot());
    engine.SetEditorEntityState({}, {});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1})==primitive.GetUUID());
    auto parent=scene.CreateEntity("Outline parent");primitive.SetParent(parent);
    assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1},nullptr,{parent.GetUUID()}).IsRoot());
    using OutlineCheck=Bazzalt::Runtime::EditorOutlineTestAccess;
    engine.SetEditorSelection({});engine.SetEditorObjectHover({},{});
    OutlineCheck::Check(engine,{},{});
    engine.SetEditorObjectHover(primitive.GetUUID(),{});
    OutlineCheck::Check(engine,{}, {primitive.GetUUID()});
    engine.SetEditorObjectHover(parent.GetUUID(),{});
    OutlineCheck::Check(engine,{}, {primitive.GetUUID()});
    engine.SetEditorObjectHover({},{});engine.SetEditorSelection({parent.GetUUID()});
    OutlineCheck::Check(engine,{primitive.GetUUID()},{});
    engine.SetEditorSelection({Bazzalt::UUID{}});
    OutlineCheck::Check(engine,{},{}); // Root cannot select all descendants.
    engine.SetEditorSelection({});
    auto terrain=scene.CreateEntity("Raised terrain outline");
    auto& heightfield=terrain.AddComponent<Bazzalt::Terrain>();
    Bazzalt::TerrainBrush brush;brush.Radius=3;brush.Strength=8;
    heightfield.ApplyBrush(Bazzalt::TerrainTool::Raise,{},brush,1);
    Bazzalt::Vec3 surface;
    assert(engine.PickEditorPrimitive({0,6,10},{0,0,-1},&surface)==terrain.GetUUID());
    assert(std::abs(surface.Y-6)<.001f&&surface.Z>0);
    assert(engine.PickEditorPrimitive({0,20,0},{0,-1,0},&surface)==terrain.GetUUID());
    assert(std::abs(surface.Y-8)<.001f);
    assert(engine.PickEditorPrimitive({10,-5,0},{-1,0,0},&surface)==terrain.GetUUID());
    assert(std::abs(surface.X-5)<.001f);
    assert(engine.PickEditorPrimitive({0,-30,0},{0,1,0},&surface)==terrain.GetUUID());
    assert(std::abs(surface.Y+20)<.001f);
    engine.SetEditorObjectHover(terrain.GetUUID(),{});
    OutlineCheck::Check(engine,{}, {terrain.GetUUID()});
    engine.SetEditorSelection({terrain.GetUUID()});
    OutlineCheck::Check(engine,{terrain.GetUUID()},{});
    engine.SetEditorObjectHover({},{});engine.SetEditorSelection({});
    engine.Shutdown();
    Bazzalt::Runtime::RenderBackend backend;assert(backend.Initialize(true));auto& assets=backend.GetAssets();
    {
        Bazzalt::Scene cameras;auto& system=cameras.AddSystem<Bazzalt::Runtime::CameraSystem>(backend);
        auto entity=cameras.CreateEntity("DoF exposure regression");auto& camera=entity.AddComponent<Bazzalt::Camera>();
        camera.PostProcessing.DepthOfField.Enabled=true;
        SystemTestAccess::Tick(system,cameras);
        assert(backend.GetActiveViews().size()==1);
        auto* view=backend.GetActiveViews().front();const auto getExposure=[&](){return filament::Exposure::exposure(view->getCamera());};const float exposure=getExposure();
        assert(view->getDepthOfFieldOptions().enabled);
        for(int i=0;i<=7;++i){
            camera.PostProcessing.AntiAliasingMode=static_cast<Bazzalt::AntiAliasing>(i);SystemTestAccess::Tick(system,cameras);
            const auto expected=Bazzalt::Runtime::ResolveAntiAliasing(camera.PostProcessing.AntiAliasingMode);
            assert((view->getAntiAliasing()==filament::View::AntiAliasing::FXAA)==expected.Fxaa);
            assert(view->getTemporalAntiAliasingOptions().enabled==expected.Temporal);
            assert(view->getMultiSampleAntiAliasingOptions().enabled==(expected.Samples>1));
            assert(view->getMultiSampleAntiAliasingOptions().sampleCount==expected.Samples);
        }
        camera.PostProcessing.Enabled=false;SystemTestAccess::Tick(system,cameras);
        assert(!view->getMultiSampleAntiAliasingOptions().enabled&&!view->getTemporalAntiAliasingOptions().enabled&&view->getAntiAliasing()==filament::View::AntiAliasing::NONE);
        camera.PostProcessing.Enabled=true;camera.PostProcessing.AntiAliasingMode=Bazzalt::AntiAliasing::TAA;SystemTestAccess::Tick(system,cameras);
        camera.PostProcessing.DepthOfField.Aperture=2.8f;SystemTestAccess::Tick(system,cameras);
        assert(std::abs(getExposure()-exposure)<exposure*1e-5f);
        assert(std::abs(view->getDepthOfFieldOptions().cocScale-16.f/2.8f)<1e-5f);
        camera.PostProcessing.DepthOfField.Aperture=32.f;SystemTestAccess::Tick(system,cameras);
        assert(std::abs(getExposure()-exposure)<exposure*1e-5f);
        assert(std::abs(view->getDepthOfFieldOptions().cocScale-.5f)<1e-5f);
        camera.PostProcessing.Exposure=1.f;SystemTestAccess::Tick(system,cameras);
        assert(std::abs(getExposure()-2.f*exposure)<exposure*1e-5f);
        camera.PostProcessing.DepthOfField.Enabled=false;SystemTestAccess::Tick(system,cameras);
        assert(!view->getDepthOfFieldOptions().enabled);
        auto& blur=entity.AddComponent<Bazzalt::GaussianBlur>();blur.Size=1.f;
        camera.PostProcessing.DepthOfField.Enabled=true;camera.PostProcessing.DepthOfField.FocusDistance=1.f;camera.PostProcessing.DepthOfField.Aperture=.5f;
        SystemTestAccess::Tick(system,cameras);
        assert(view->getCamera().getFocusDistance()==1.f);
        assert(view->getDepthOfFieldOptions().cocScale==32.f);
        assert(view->getDepthOfFieldOptions().maxForegroundCOC==0);
        camera.PostProcessing.DepthOfField.Enabled=false;SystemTestAccess::Tick(system,cameras);
        assert(view->getCamera().getFocusDistance()==.001f);
        assert(view->getDepthOfFieldOptions().maxForegroundCOC==1);
        blur.Enabled=false;SystemTestAccess::Tick(system,cameras);
        assert(!view->getDepthOfFieldOptions().enabled);
    }
    {
        Bazzalt::Scene lights;auto& system=lights.AddSystem<Bazzalt::Runtime::LightSystem>(backend);
        auto entity=lights.CreateEntity("Live light");auto& value=entity.AddComponent<Bazzalt::Light>();
        const auto check=[&](){int checked=0;backend.GetScene().forEach([&](utils::Entity resource){
            auto& manager=backend.GetEngine().getLightManager();const auto instance=manager.getInstance(resource);if(!instance)return;
            // Native getters return candela for punctual lights, not input lumens.
            const float intensity=value.Type==Bazzalt::LightType::Point?value.Intensity/(4*Bazzalt::Pi):value.Type==Bazzalt::LightType::Spot?value.Intensity/Bazzalt::Pi:value.Intensity;
            assert(std::abs(manager.getIntensity(instance)-intensity)<.001f);
            if(value.Type==Bazzalt::LightType::Sun){assert(std::abs(manager.getSunAngularRadius(instance)-Bazzalt::ToDegrees(value.SunAngularRadius))<1e-4f);assert(manager.getSunHaloSize(instance)==value.SunHaloSize);assert(manager.getSunHaloFalloff(instance)==value.SunHaloFalloff);}
            if(value.Type==Bazzalt::LightType::Point||value.Type==Bazzalt::LightType::Spot)assert(std::abs(manager.getFalloff(instance)-value.Range)<1e-4f);
            if(value.Type==Bazzalt::LightType::Spot)assert(std::abs(manager.getSpotLightOuterCone(instance)-value.OuterConeAngle)<1e-4f);
            ++checked;
        });assert(checked==(value.Enabled?1:0));};
        for(const auto type:{Bazzalt::LightType::Point,Bazzalt::LightType::Spot,Bazzalt::LightType::Sun,Bazzalt::LightType::Directional,Bazzalt::LightType::Point}){
            value.Type=type;SystemTestAccess::Tick(system,lights);check();
            value.Range+=2;value.Intensity+=50;value.SunAngularRadius=Bazzalt::ToRadians(2);value.SunHaloSize=4;value.SunHaloFalloff=12;value.OuterConeAngle=Bazzalt::ToRadians(45);
            SystemTestAccess::Tick(system,lights);check();
        }
        value.Enabled=false;SystemTestAccess::Tick(system,lights);check();
    }
    auto* editorView=backend.GetEngine().createView();auto* gameView=backend.GetEngine().createView();
    Bazzalt::Runtime::RenderBackend::ConfigureEditorFog(*editorView,1);
    const auto fog=editorView->getFogOptions();assert(!fog.enabled);
    assert(editorView->getAmbientOcclusionOptions().enabled);
    assert(!gameView->getFogOptions().enabled);backend.GetEngine().destroy(editorView);backend.GetEngine().destroy(gameView);
    Bazzalt::PrimitiveObject object;const auto owner=Bazzalt::UUID::Generate();const auto handle=assets.CreatePrimitive(object);assets.SetEditorOwner(handle,owner);
    const auto count=backend.GetScene().getRenderableCount();assert(count>0);
    assets.BeginEditorView({owner});assert(backend.GetScene().getRenderableCount()==count-1);
    assets.EndEditorView();assert(backend.GetScene().getRenderableCount()==count);
    assets.DestroyPrimitive(handle);
    // Guides must render after the skybox, without writing scene depth.
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,false}});
    auto checkGuides=[&](bool outline){int checked=0;backend.GetScene().forEach([&](utils::Entity entity){
        auto& renderables=backend.GetEngine().getRenderableManager();const auto instance=renderables.getInstance(entity);if(!instance)return;
        const auto* material=renderables.getMaterialInstanceAt(instance,0);if(std::string_view(material->getMaterial()->getName())!="EditorGuide")return;
        assert(material->getMaterial()->getBlendingMode()==filament::BlendingMode::TRANSPARENT);
        assert(!material->getMaterial()->isDoubleSided());
        assert(!material->isDepthWriteEnabled());assert(material->isDepthCullingEnabled()!=outline);++checked;
    });assert(checked==1);};
    checkGuides(false);
    const auto uploads=backend.GetStatistics().at("guide_uploads");
    for(int i=0;i<20;++i)backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,false}});
    assert(backend.GetStatistics().at("guide_uploads")==uploads);
    // Long frustum edges and segments crossing the editor camera must use the
    // same batched guide resource, including growth, shrink and invalid input.
    backend.SetEditorGuides({{0,0,0,1000,500,-1000,1,1,1,1,false},{0,0,1,0,0,-1000,1,1,1,1,false}});checkGuides(false);
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,false}});checkGuides(false);
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,true}});checkGuides(true);
    backend.SetEditorGuides({});backend.Shutdown();
    return 0;
}
