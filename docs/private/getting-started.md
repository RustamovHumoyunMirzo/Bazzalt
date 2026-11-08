# Getting started

## Run the editor from source

During editor development, do not rebuild the Nuitka applications or installer. On Windows run:

```powershell
.\scripts\run_editor.ps1 -Project "C:\path\to\Game.bproject"
```

The launcher runs `bazzalt_editor.py` directly. Python, Qt, themes, panels, and other editor-only changes are picked up on the next launch without compiling. It checks native source timestamps and rebuilds only `_bazzalt_runtime` when C++ changed. Use `-NoBuild` to skip that check or `-RebuildNative` to force it. If `-Project` is omitted, the Alpha fixture project is used.

On Linux or macOS, use `./scripts/run_editor.sh /path/to/Game.bproject`.

## Requirements and dependency strategy

Use CMake, Git, a supported C++20 toolchain and Python with the repository's
editor dependencies. Windows uses MSVC. Linux x64 uses Clang/libc++ to match the
Filament SDK. Linux x86 uses its configured multilib runtime. macOS uses the
matching Apple toolchain. Editor and Hub require a 64-bit machine.

Start from a supplied matching SDK or an existing configured build. Production
Filament/Qt and cross-architecture dependency builds belong in GitHub Actions.
Review the matching platform workflow before setting up a new toolchain.

Fetch scripts pin dependencies under `deps`. For the desktop source build, the
current dependency set is shown below. Fetching should use supplied archives where
available. Do not start large source builds as part of routine contributor work.

```powershell
$Dependencies = 'entt','rapidyaml','filament','sdl3','stb_image','lua','sol2','miniaudio','jolt','recast','yojimbo','freetype','text_shaping','pybind11'
foreach ($Dependency in $Dependencies) { & "./scripts/get_$Dependency.ps1" }
```

```bash
for dependency in entt rapidyaml filament sdl3 stb_image lua sol2 miniaudio jolt recast yojimbo freetype text_shaping pybind11; do
    ./scripts/get_${dependency}.sh
done
```

Provider replacements can select their own solver dependencies. See
[module development](module-development.md). Use [verification](verification.md)
for incremental targets and CI coverage.

The Filament SDK provides runtime libraries plus `matc` and `filamesh`. Override
their locations with `BAZZALT_FILAMENT_DIR`, `BAZZALT_MATC_EXECUTABLE`, and
`BAZZALT_FILAMESH_EXECUTABLE` when configuring CMake.

## Configure, build, and test

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
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
