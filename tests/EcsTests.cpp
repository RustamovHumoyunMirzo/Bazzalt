#include <cassert>

#include "Bazzalt/Component.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/System.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/SceneQueryBounds.h"

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

    const Bazzalt::UUID entityUuid = entity.GetUUID();
    assert(entityUuid);
    assert(scene.GetEntity(entityUuid) == entity);
    Bazzalt::UUID parsedUuid;
    assert(Bazzalt::UUID::TryParse(entityUuid.ToString(), parsedUuid));
    assert(parsedUuid == entityUuid);

    auto& velocity = entity.AddComponent<Velocity>(Bazzalt::Vec3{2.0f, 0.0f, 0.0f});
    assert(entity.HasComponent<Velocity>());
    assert(&entity.GetComponent<Velocity>() == &velocity);
    assert(entity.TryGetComponent<Velocity>() == &velocity);

    scene.AddSystem<MovementSystem>();
    assert(scene.HasSystem<MovementSystem>());

    entity.RemoveComponent<Velocity>();
    assert(!entity.HasComponent<Velocity>());

    const Bazzalt::UUID parentUuid{1, 2};
    const Bazzalt::UUID childUuid{3, 4};
    const Bazzalt::UUID grandchildUuid{5, 6};
    Bazzalt::Entity parent = scene.CreateEntity(parentUuid, "Parent");
    Bazzalt::Entity child = scene.CreateEntity(childUuid, "Child");
    Bazzalt::Entity grandchild = scene.CreateEntity(grandchildUuid, "Grandchild");
    assert(parent.AddChild(child));
    assert(grandchild.SetParent(child));
    assert(child.GetParent() == parent);
    assert(parent.GetChildren().size() == 1);
    assert(parent.IsAncestorOf(grandchild));
    assert(!parent.SetParent(grandchild)); // A cycle is rejected.

    parent.GetComponent<Bazzalt::Transform>().Position = {10.0f, 0.0f, 0.0f};
    child.GetComponent<Bazzalt::Transform>().Position = {2.0f, 0.0f, 0.0f};
    grandchild.GetComponent<Bazzalt::Transform>().Position = {1.0f, 0.0f, 0.0f};
    assert(grandchild.GetWorldMatrix().TransformPoint({}).X == 13.0f);

    auto camera = scene.CreateEntity("Query Camera");
    camera.GetComponent<Bazzalt::Transform>().Position = {0, 0, 5};
    camera.AddComponent<Bazzalt::Camera>();
    auto target = scene.CreateEntity("Query Target");
    target.AddComponent<Bazzalt::SceneQueryBounds>().LayerMask = 0x1;
    const auto ray = scene.ScreenPointToRay(camera, {400, 300}, {800, 600});
    const auto hit = scene.Raycast(ray);
    assert(hit && hit.Target == target);
    assert(hit.Distance > 4.0f && hit.Distance < 5.0f);
    assert(scene.Pick(camera, {400, 300}, {800, 600}).Target == target);
    assert(scene.RaycastAll(ray).size() == 1);
    assert(scene.OverlapSphere({}, 1.0f).size() == 1);
    assert(scene.OverlapBox({}, {1, 1, 1}).size() == 1);
    Bazzalt::SceneQueryOptions hiddenLayer;
    hiddenLayer.LayerMask = 0x2;
    assert(!scene.Raycast(ray, hiddenLayer));

    scene.DestroyEntity(parent);
    assert(!scene.GetEntity(parentUuid));
    assert(!scene.GetEntity(childUuid));
    assert(!scene.GetEntity(grandchildUuid));
    return 0;
}
