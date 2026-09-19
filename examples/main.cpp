#include "Runtime/Engine.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt
{
    class MovementSystem final : public ComponentSystem<Transform>
    {
    protected:
        void OnUpdate(Bazzalt::Scene &scene, float deltaTime) override
        {
            auto view = GetView(scene.GetRegistry());
            for (auto entity : view)
            {
                view.get<Bazzalt::Transform>(entity).Position.X += deltaTime;
            }
        }
    };
}

int main(int argc, char **argv)
{
    Engine engine;

    // Initialize core subsystems
    if (!engine.Init())
    {
        return -1;
    }

    Bazzalt::Scene scene;
    auto player = scene.CreateEntity("Player");
    player.GetComponent<Bazzalt::Transform>().Position.X = 1.0f;
    scene.AddSystem<Bazzalt::MovementSystem>();

    // Main engine execution loop driven externally
    while (!engine.ShouldClose())
    {
        engine.Update();
        scene.Update(engine.GetDeltaTime());
    }

    // Explicit shutdown on exit
    engine.Shutdown();

    return 0;
}