# Scene queries and picking

Scene queries are lightweight ECS queries for editor selection, interaction,
visibility tools, and gameplay targeting. They do not create a physics world,
rigid body, contact pair, or simulation state.

Entities participate by adding `SceneQueryBounds`:

```cpp
#include <Bazzalt/Components/SceneQueryBounds.h>

auto& bounds = entity.AddComponent<Bazzalt::SceneQueryBounds>();
bounds.Shape = Bazzalt::SceneQueryShape::Box;
bounds.Center = {0.0f, 1.0f, 0.0f};
bounds.Extents = {0.5f, 1.0f, 0.5f};
bounds.LayerMask = 1u << 3;
```

Bounds are local to the entity and follow its complete parent transform.
Raycasts handle rotation and non-uniform scale through local-space testing.

## Camera picking

`Pick` and `PickAll` receive screen pixels with a top-left origin and the full
presentation size. They account for perspective/orthographic projection and
the camera's split-screen viewport.

```cpp
Bazzalt::SceneQueryOptions options;
options.LayerMask = 1u << 3;
options.MaxDistance = 500.0f;

auto hit = scene.Pick(cameraEntity, mousePosition, windowSize, options);
if (hit) {
    Select(hit.Target);
}
```

`ScreenPointToRay` is available when a caller needs to reuse the ray. It throws
for an invalid camera, invalid presentation size, or a point outside that
camera's viewport.

## World queries

- `Raycast` returns the nearest hit.
- `RaycastAll` returns every hit sorted by distance, then UUID.
- `OverlapSphere` returns entities whose world bounds overlap a sphere.
- `OverlapBox` returns entities whose world bounds overlap an axis-aligned box.

`SceneQueryOptions` provides a 32-bit layer mask, maximum ray distance,
disabled-bound inclusion, and an optional entity predicate. Overlap results are
sorted by UUID for deterministic editor and serialization workflows.

Ray hits contain the target entity, world point, world normal, distance, and a
`StartedInside` flag. Overlap queries use conservative world AABBs; ray tests
remain exact against the authored local box or sphere.
