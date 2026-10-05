#include <cassert>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Runtime/Engine.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/LightGuides.h"

namespace Bazzalt::Runtime {
struct EditorOutlineTestAccess {
    static void Check(Engine& engine,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered){
        engine.UpdateEditorOverlays();
        assert(engine.m_renderBackend->m_outlineSelected==selected);
        assert(engine.m_renderBackend->m_outlineHovered==hovered);
    }
};
}
#include "Rendering/LightParameters.h"
#include "Rendering/RenderSystems.h"
#include "Rendering/RenderBackend.h"
#include "Rendering/RenderAssets.h"
#include <filament/Scene.h>
#include <filament/Engine.h>
#include <filament/View.h>
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

int main(int argc,char** argv) {
    if(argc>1&&std::string_view(argv[1])=="--gpu-dof")return CheckDofPixels();
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
    engine.SetEditorEntityState({}, {primitive.GetUUID()});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1}).IsRoot());
    engine.SetEditorEntityState({primitive.GetUUID()}, {});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1}).IsRoot());
    engine.SetEditorEntityState({}, {});assert(engine.PickEditorPrimitive({0,0,5},{0,0,-1})==primitive.GetUUID());
    auto parent=scene.CreateEntity("Outline parent");primitive.SetParent(parent);
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
    const auto fog=editorView->getFogOptions();assert(fog.enabled);assert(std::isfinite(fog.cutOffDistance));assert(fog.cutOffDistance>180&&fog.cutOffDistance<1e19f);
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
    // Long frustum edges and segments crossing the editor camera must use the
    // same batched guide resource, including growth, shrink and invalid input.
    backend.SetEditorGuides({{0,0,0,1000,500,-1000,1,1,1,1,false},{0,0,1,0,0,-1000,1,1,1,1,false}});checkGuides(false);
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,false}});checkGuides(false);
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,true}});checkGuides(true);
    backend.SetEditorGuides({});backend.Shutdown();
    return 0;
}
