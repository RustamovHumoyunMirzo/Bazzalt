#include <cassert>

#include "Bazzalt/Component.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/System.h"

struct Velocity : Bazzalt::Component {
    Bazzalt::Vec3 Value{};

    Velocity() = default;
    explicit Velocity(Bazzalt::Vec3 value) : Value(value) {}
};

class MovementSystem final
    : public Bazzalt::ComponentSystem<Bazzalt::Transform, Velocity> {
protected:
    void OnUpdate(Bazzalt::Scene& scene, float deltaTime) override {
        auto view = GetView(scene.GetRegistry());
        for (const auto handle : view) {
            auto& transform = view.get<Bazzalt::Transform>(handle);
            transform.Position += view.get<Velocity>(handle).Value * deltaTime;
        }
    }
};

int main() {
    Bazzalt::Scene scene;
    Bazzalt::Entity entity = scene.CreateEntity("Custom component entity");

    auto& velocity = entity.AddComponent<Velocity>(Bazzalt::Vec3{2.0f, 0.0f, 0.0f});
    assert(entity.HasComponent<Velocity>());
    assert(&entity.GetComponent<Velocity>() == &velocity);
    assert(entity.TryGetComponent<Velocity>() == &velocity);

    scene.AddSystem<MovementSystem>();
    scene.Update(0.5f);
    assert(entity.GetComponent<Bazzalt::Transform>().Position.X == 1.0f);

    entity.RemoveComponent<Velocity>();
    assert(!entity.HasComponent<Velocity>());
    return 0;
}
