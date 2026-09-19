# BAZZALT Engine Plan

## Building

BAZZALT uses EnTT for its ECS. Fetch the pinned dependency before configuring:

```powershell
./scripts/get_entt.ps1
cmake -S . -B build
cmake --build build --config Debug
```

On Linux or macOS, run `./scripts/get_entt.sh` instead.

## ECS API

Public engine headers live under `include/Bazzalt`; `include/Runtime` is reserved
for private runtime/editor integration. Entities created by a scene receive a
`Name` and `Transform` component automatically.

```cpp
#include <Bazzalt/Scene.h>

struct Velocity : Bazzalt::Component {
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
            transform.Position.X += velocity.Value.X * deltaTime;
        }
    }
};

Bazzalt::Scene scene;
auto player = scene.CreateEntity("Player");
player.AddComponent<Velocity>();
scene.AddSystem<MovementSystem>();
scene.Update(deltaTime);
```

## Math API

Include `<Bazzalt/Math.h>` for `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, and
`Quaternion`. Angles are expressed in radians; `ToRadians` and `ToDegrees`
are provided for conversion. Matrices use row-major storage, multiply column
vectors, and compose transforms as translation, rotation, then scale.

```cpp
using namespace Bazzalt;

Quaternion turn = Quaternion::FromAxisAngle({0, 1, 0}, ToRadians(90.0f));
Mat4 model = Mat4::Transform({2, 0, 0}, turn, {1, 1, 1});
Vec3 worldPoint = model.TransformPoint({0, 0, -1});

auto& transform = player.GetComponent<Transform>();
transform.Rotation = turn;
Mat4 entityModel = transform.GetMatrix();
```

TODOs, frameworks and libraries we should use to build BAZZALT engine. Core engine should be written in C/C++. Engine should work without even editor. Editor binds the engine using ctypes or libs like this to hook, modify methods. Game and Editor should be same, Editor hook should not affect on game. They use same engine, but hooking happens at runtime, Editor just edits the metadatas, asset pipeline and compiling and etc. Loading them and executing are done by core engine, game is just an executable that loads the engine with pointing to assets. Engine is not edited on game build, executable is not edited, only assets and metadatas are dynamic.

## Frameworks & Libraries

### Editor

- Language: **Python 3**
- GUI: **PySide6**

### Rendering

- Core: **Google's Filament**
- Materials: **Filament's filamat**

### Assets & Formats

- glTF: **Filament's gltfio implementation**
- Images: **stb_image**

### Windowing & Input

*In editor, filament works as offscreen (rendering in texture), so it won't create the window in editor, only after build, it can handle os windowing. Therefore, same engine used for game and editor: `bazzalt` `.dll` or other platform specific extension*

- Core: **GLFW**

### Physics

- Core: **Jolt Physics**

### ECS

- Core: **EnTT**

### Audio

- Core: **miniaudio**

### Scripting

- Core native: **C/C++**
- Lua: **Official Lua JIT**

## Workflow

### Script Compilation

Since engine is static and only assets and scripts are dynamic, we should handle scripts somehow. Native ones are easy, we compile them to libs, at runtime, engine loads them. What about Lua? Firsty, we'll covert it to Lua Bytcode using official method. Then, we'll turn it to bytes and compile it inside native code, where is that native code and what if user writes only Lua code? Our editor adds one native code no matter user writes lua or no. That native is designed to contain lua codes in itself as byte arrays. And our static engine loads that one native code, after reading it, engine can execute loaded Lua at runtime. That native lib called `monol.dll`, always same name but its dynamic.

### Main Loop

Runtime should never own a loop, it owns only commands. Runtime is useless without parent process. Runtime decides when frames happen. Only external application or some process holding the runtime can have the loop and run the engine every deltatime.
