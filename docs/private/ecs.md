# ECS, components, and systems

## Entities and defaults

`Scene::CreateEntity` returns a lightweight `Entity` facade. Every ordinary
entity receives `Identity`, `Name`, `Transform`, and `Hierarchy` components.

```cpp
Bazzalt::Scene scene;
auto player = scene.CreateEntity("Player");
player.GetComponent<Bazzalt::Transform>().Position = {0.0f, 1.0f, 0.0f};
```

An entity facade becomes invalid after destruction. Check `IsValid()` or its
explicit boolean conversion before reusing a retained handle after structural
scene changes.

## Custom components

User components derive from the empty `Bazzalt::Component` marker. They should
normally be plain data and must not own engine resources.

```cpp
#include <Bazzalt/Component.h>
#include <Bazzalt/Math.h>

struct CharacterController final : Bazzalt::Component {
    float Speed = 5.0f;
    Bazzalt::Vec3 DesiredDirection{};
};
```

The marker provides compile-time validation and has no state or virtual table.
Components may have constructors and helper methods when compatible with their
creation and serialization code.

## Component operations

```cpp
auto& health = entity.AddComponent<Health>();
Health replacement;
replacement.Value = 75.0f;
entity.AddOrReplaceComponent<Health>(replacement);

if (entity.HasComponent<Health>())
    entity.GetComponent<Health>().Value -= 10.0f;

if (auto* optional = entity.TryGetComponent<Health>())
    optional->Value = 100.0f;

entity.RemoveComponent<Health>();
```

`AddComponent` expects absence. `GetComponent` expects presence. Use
`AddOrReplaceComponent` and `TryGetComponent` when either state is valid. Every
template argument must derive from `Component`.

Avoid structural mutations against the exact EnTT view currently being
iterated unless its invalidation rules are understood. Collect work and mutate
after the query when in doubt.

## Systems

Derive from `System`, or use `ComponentSystem<T...>` for a typed view helper.

```cpp
class LifetimeSystem final : public Bazzalt::ComponentSystem<Lifetime> {
protected:
    void OnCreate(Bazzalt::Scene&) override {}

    void OnUpdate(Bazzalt::Scene& scene, float deltaTime) override {
        auto view = GetView(scene.GetRegistry());
        for (auto handle : view)
            view.get<Lifetime>(handle).Remaining -= deltaTime;
    }

    void OnDestroy(Bazzalt::Scene&) override {}
};
```

Lifecycle rules:

- `OnCreate` runs once when installed.
- `OnUpdate` runs per host-dispatched scene update while enabled.
- `OnDestroy` runs on removal or scene/system teardown.
- Systems execute in registration order.
- Adding an existing system type returns that instance.

```cpp
auto& system = scene.AddSystem<LifetimeSystem>();
system.SetEnabled(false);
bool present = scene.HasSystem<LifetimeSystem>();
auto& same = scene.GetSystem<LifetimeSystem>();
scene.RemoveSystem<LifetimeSystem>();
```

`GetSystem` requires presence. `RemoveSystem` returns `false` when absent.

## Direct registry access

`Scene::GetRegistry()` supports advanced EnTT queries. Do not manually create
entities, edit `Identity`/`Hierarchy`, bypass scene destruction, or persist
`entt::entity` values; this can break UUID and hierarchy invariants.
