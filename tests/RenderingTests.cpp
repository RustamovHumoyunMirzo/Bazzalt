#include <cassert>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Runtime/Engine.h"

int main() {
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
    assert(engine.Init());

    auto& scene = engine.GetScene();
    auto camera = scene.CreateEntity("Camera");
    camera.GetComponent<Bazzalt::Transform>().Position = {0.0f, 0.0f, 5.0f};
    camera.AddComponent<Bazzalt::Camera>().Viewport = left;
    auto& cameraComponent = camera.GetComponent<Bazzalt::Camera>();
    cameraComponent.PostProcessing.DepthOfField.Enabled = true;
    cameraComponent.PostProcessing.DepthOfField.FocusDistance = 5.0f;
    cameraComponent.PostProcessing.CustomEffects = std::move(customStack);

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
    engine.Shutdown();
    return 0;
}
