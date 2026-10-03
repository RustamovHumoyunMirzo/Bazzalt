# Getting started

## Run the editor from source

During editor development, do not rebuild the Nuitka applications or installer. On Windows run:

```powershell
.\scripts\run_editor.ps1 -Project "C:\path\to\Game.bproject"
```

The launcher runs `bazzalt_editor.py` directly. Python, Qt, themes, panels, and other editor-only changes are picked up on the next launch without compiling. It checks native source timestamps and rebuilds only `_bazzalt_runtime` when C++ changed. Use `-NoBuild` to skip that check or `-RebuildNative` to force it. If `-Project` is omitted, the Alpha fixture project is used.

On Linux or macOS, use `./scripts/run_editor.sh /path/to/Game.bproject`.

## Requirements

- CMake 3.16 or newer
- A C++20 compiler
- Git, `curl`, and an archive tool used by the dependency scripts
- A graphics driver supported by the selected Filament backend

BAZZALT pins EnTT, SDL3, rapidyaml, and the Filament SDK through scripts in
`scripts/`. Dependencies are placed under `deps/` and do not expand the public
include surface.

## Fetch dependencies

Windows PowerShell:

```powershell
./scripts/get_entt.ps1
./scripts/get_sdl3.ps1
./scripts/get_rapidyaml.ps1
./scripts/get_filament.ps1
```

Linux or macOS:

```bash
./scripts/get_entt.sh
./scripts/get_sdl3.sh
./scripts/get_rapidyaml.sh
./scripts/get_filament.sh
```

The Filament SDK provides runtime libraries plus `matc` and `filamesh`. Override
their locations with `BAZZALT_FILAMENT_DIR`, `BAZZALT_MATC_EXECUTABLE`, and
`BAZZALT_FILAMESH_EXECUTABLE` when configuring CMake.

## Configure, build, and test

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The repository builds a shared `Bazzalt` target, a public API example, and test
executables. A CMake consumer can link it directly:

```cmake
add_subdirectory(path/to/BAZZALT)
target_link_libraries(MyGame PRIVATE Bazzalt)
```

The editor and native gameplay modules use the same shared engine. Builds stage
public headers, EnTT headers, and the link library in `build/ScriptSDK`.
See [Native gameplay script SDK](script-sdk.md) for module linking and lifetime.

## First component and system

```cpp
#include <Bazzalt/Scene.h>

struct Velocity final : Bazzalt::Component {
    Bazzalt::Vec3 Value{};
};

class MovementSystem final
    : public Bazzalt::ComponentSystem<Bazzalt::Transform, Velocity> {
protected:
    void OnUpdate(Bazzalt::Scene& scene, float deltaTime) override {
        auto view = GetView(scene.GetRegistry());
        for (auto handle : view) {
            auto& transform = view.get<Bazzalt::Transform>(handle);
            const auto& velocity = view.get<Velocity>(handle);
            transform.Position += velocity.Value * deltaTime;
        }
    }
};

int main() {
    Bazzalt::Scene scene;
    auto player = scene.CreateEntity("Player");
    auto& velocity = player.AddComponent<Velocity>();
    velocity.Value = {1.0f, 0.0f, 0.0f};
    scene.AddSystem<MovementSystem>();
}
```

This demonstrates extension types only. In an editor/game build, the runtime
host owns initialization and dispatches updates. User code does not instantiate
the private engine or call its frame methods.

## Naming convention

Public methods use PascalCase: `AddComponent`, `HasComponent`, `LoadScene`, and
`GetWorldMatrix`. Lifecycle hooks are `OnCreate`, `OnUpdate`, and `OnDestroy`.
