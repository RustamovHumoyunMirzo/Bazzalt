#include <cassert>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Runtime/Engine.h"
#include "Rendering/PrimitiveGeometry.h"
#include "Rendering/LightGuides.h"
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
#include <cmath>
#include <string_view>
#include <limits>

struct SystemTestAccess : Bazzalt::System {
    static void Tick(Bazzalt::System& system,Bazzalt::Scene& scene){
        constexpr auto update=&SystemTestAccess::OnUpdate;(system.*update)(scene,0);
    }
};

int main() {
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
    engine.Shutdown();
    Bazzalt::Runtime::RenderBackend backend;assert(backend.Initialize(true));auto& assets=backend.GetAssets();
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
    backend.SetEditorGuides({{0,0,0,1,0,0,1,1,1,1,true}});checkGuides(true);
    backend.SetEditorGuides({});backend.Shutdown();
    return 0;
}
