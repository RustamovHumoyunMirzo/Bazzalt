# BAZZALT


BAZZALT is an editor-first C++20 game engine in active alpha development. It
combines a native runtime with a PySide6 editor and Hub while keeping low-level
engine lifetime, rendering, importing, and persistence away from game code.

> **Alpha status:** core workflows are functional, but APIs and file formats may
> still change. BAZZALT is not yet recommended for production projects.

## Highlights

- **Extensible ECS** — EnTT-backed entities, user components and lifecycle
  systems, stable 128-bit UUIDs, hierarchy, and a permanent root entity.
- **Scenes and projects** — versioned YAML stored in `.bscene` and project
  files, deferred loading, unknown-component preservation, and editor save state.
- **Asset pipeline** — UUID assets, YAML `.meta` files, importer registry,
  caching, glTF model hierarchies, textures, materials, and meshes.
- **Rendering** — private Google Filament backend with native Qt swap chains,
  separate Scene/Game views, multiple cameras and viewports, lights, meshes,
  environments, and camera post-processing.
- **Editor tooling** — dockable Scene, Game, Hierarchy, Inspector, Console,
  Output, and Asset Browser panels; transform gizmos, adaptive world grid,
  scene picking, camera controls, drag and drop, themes, and localization.
- **Public foundations** — vectors, matrices, quaternions, rays, scene queries,
  raycasts, picking, components, systems, and renderer-independent data types.
- **Distribution** — version-aware BAZZALT Hub, standalone Nuitka builds, and a
  Windows Inno Setup packaging pipeline.

## Images

Editor - dark mode:

<image src="./media/screenshot_alpha.png" width="1024">

## Architecture

Only `include/Bazzalt` is the supported game-facing C++ API. Filament, EnTT,
editor integration, the application loop, saving, and importing remain private.
Users create components and systems, edit scenes through BAZZALT, and request
scene loads without owning or overriding the engine loop.

```cpp
#include <Bazzalt/Component.h>
#include <Bazzalt/Scene.h>

struct Health final : Bazzalt::Component {
    float Value = 100.0f;
};

Bazzalt::Scene scene;
Bazzalt::Entity player = scene.CreateEntity("Player");
player.AddComponent<Health>();
```

## Build from source

Requirements: CMake 3.16+, a C++20 compiler, Python 3 with PySide6 for the
editor, and supported Filament tools. Fetch the pinned dependencies first:

```powershell
./scripts/get_entt.ps1
./scripts/get_rapidyaml.ps1
./scripts/get_filament.ps1
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Use the matching `.sh` scripts on Linux or macOS. For Windows production builds:

```powershell
python -m pip install -r requirements-build.txt
./scripts/build_production.ps1 -Version 1.0.0 -CreateInstaller
```

## Documentation

- [C++ behaviors and bundled compiler](docs/private/scripting.md)

The full engine handbook and API reference live in [`docs/`](docs/private/README.md):

- [Getting started](docs/private/getting-started.md)
- [Architecture and ownership](docs/private/architecture.md)
- [ECS, components, and systems](docs/private/ecs.md)
- [Scenes, hierarchy, and serialization](docs/private/scenes-and-serialization.md)
- [Asset database and import pipeline](docs/private/assets.md)
- [Rendering](docs/private/rendering.md)
- [Scene queries](docs/private/scene-queries.md)
- [Model hierarchies](docs/private/model-hierarchies.md)
- [Math](docs/private/math.md)
- [Public API reference](docs/private/api-reference.md)
- [Editor runtime integration](docs/private/editor-runtime-integration.md)
- [Editor panels](docs/private/editor-panels.md)
- [Editor toolbar and gizmos](docs/private/editor-toolbar-and-gizmos.md)
- [Hub, data, and version management](docs/private/hub-and-editor-data.md)
- [Production packaging](docs/private/production-packaging.md)
- [Roadmap and planned technology](docs/private/roadmap.md)

## Repository layout

| Path | Purpose |
|---|---|
| `include/Bazzalt` | Supported public C++ headers |
| `src` | Private engine, runtime, renderer, and editor bridge |
| `Editor` | PySide6 editor UI, resources, services, and tests |
| `Launcher` | Hub web interface |
| `runtime/resources` | Runtime shaders and embedded resources |
| `tests` | Native engine tests |
| `docs` | Engine and editor handbook |
| `scripts` | Dependency, production, and installer automation |

## Contributing

Issues and focused pull requests are welcome. Include tests for behavior
changes, keep third-party types out of `include/Bazzalt`, preserve UUID and YAML
forward compatibility, and run the native and editor test suites before
submitting changes.

## License

This project uses a modular multi-license structure:

* **Core Engine:** Licensed under the **Apache License 2.0**. You can find the full terms in the root [LICENSE](LICENSE) file.
* **Editor (`/Editor`):** Licensed under the **GNU Lesser General Public License v3 (LGPLv3)**. The specific terms for the editor are located in the [Editor/LICENSE](Editor/LICENSE) file. 

This structure allows the core engine to remain permissive under Apache 2.0 while ensuring full legal and technical compliance with the [PySide6 (Qt)](https://www.qt.io/qt-for-python) framework used by the editor.
