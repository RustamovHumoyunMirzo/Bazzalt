# BAZZALT

BAZZALT is an editor-first C++20 game engine foundation. Its public API is a
renderer-independent ECS built around scenes, entities, user-defined
components and systems. Project orchestration, serialization, importing, the
application loop, and Filament resource ownership stay inside the runtime and
editor host.

## Current capabilities

- EnTT-backed ECS with extensible components and lifecycle systems
- Stable 128-bit UUIDs, a permanent root entity, and parent-child hierarchy
- Vector, matrix, and quaternion math
- Versioned YAML project and scene formats with unknown-component preservation
- UUID asset database with YAML `.meta` files and disposable import cache
- Filament rendering foundation with cameras, lights, meshes, glTF, textures,
  compiled materials, and filamesh assets
- Narrow public facades for asset lookup and deferred scene loading

## Quick start

BAZZALT uses EnTT for its ECS. Fetch the pinned dependency before configuring:

```powershell
./scripts/get_entt.ps1
./scripts/get_rapidyaml.ps1
./scripts/get_filament.ps1
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Use the matching `.sh` scripts on Linux or macOS.

```cpp
#include <Bazzalt/Scene.h>

struct Health final : Bazzalt::Component {
    float Value = 100.0f;
};

Bazzalt::Scene scene;
auto player = scene.CreateEntity("Player");
player.AddComponent<Health>();
```

## Documentation

The full engine handbook and API reference live in [`docs/`](docs/README.md):

- [Getting started](docs/getting-started.md)
- [Architecture and ownership](docs/architecture.md)
- [ECS, components, and systems](docs/ecs.md)
- [Scenes, hierarchy, and serialization](docs/scenes-and-serialization.md)
- [Asset database and import pipeline](docs/assets.md)
- [Rendering](docs/rendering.md)
- [Math](docs/math.md)
- [Public API reference](docs/api-reference.md)
- [Roadmap and planned technology](docs/roadmap.md)

Public headers are under `include/Bazzalt`. Headers under `src/Runtime` and
`src/Rendering` are implementation details and must not be included by game
modules.
