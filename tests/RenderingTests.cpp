#include <cassert>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Runtime/Engine.h"

int main() {
    Bazzalt::Runtime::Engine engine;
    assert(engine.Init());

    auto& scene = engine.GetScene();
    auto camera = scene.CreateEntity("Camera");
    camera.GetComponent<Bazzalt::Transform>().Position = {0.0f, 0.0f, 5.0f};
    camera.AddComponent<Bazzalt::Camera>();

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
