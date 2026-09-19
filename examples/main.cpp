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

int main()
{
    // The runtime/editor owns the engine loop. This standalone example only
    // demonstrates the public ECS extension API used by game modules.
    Bazzalt::Scene scene;
    auto player = scene.CreateEntity("Player");
    player.GetComponent<Bazzalt::Transform>().Position.X = 1.0f;
    scene.AddSystem<Bazzalt::MovementSystem>();
    return 0;
}
