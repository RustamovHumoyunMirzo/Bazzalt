# Native gameplay script SDK

Gameplay modules link to the same shared Bazzalt engine as the editor. They are
not separate engine copies and do not own initialization, rendering, or the loop.
All headers under `include/Bazzalt` are available, together with EnTT's public
headers. Private headers under `src/Runtime` are not shipped in the script SDK.
Access restrictions still apply: including a header does not make private
serialization or engine-loop methods callable.

## Normal API usage

```cpp
#include <Bazzalt/Script.h>
#include <Bazzalt/Math.h>
#include <Bazzalt/Entity.h>
#include <Bazzalt/Components/Transform.h>

COMPONENT(Mover) {
public:
    PROPERTY(Bazzalt::Entity, Target, {})
    PROPERTY(float, Speed, 5.0f)

    void OnUpdate(float deltaTime) override {
        if (!Target) return;
        auto transform = Target.GetWorldTransform();
        transform.Translate(Bazzalt::Vec3{Speed * deltaTime, 0, 0});
        Target.SetWorldTransform(transform);
    }
};
```

Assign entity properties using the inspector picker or hierarchy drag-and-drop.
Unassigned or missing entity properties resolve to an invalid entity. A behavior
can obtain its own entity using `GetEntity()` and its scene using
`GetEntity().GetScene()`. `SceneManager::GetActiveScene()` returns the actual
active runtime scene. Ordinary component access, hierarchy, scene queries,
asset lookup, math, time, input, and custom ECS systems use their existing APIs.
Entity handles are non-owning: do not retain them after their scene is destroyed.
Use `EntityReference(UUID)` and `Resolve()` for a reference that resolves afresh.

## Build and distribution

Building Bazzalt stages `build/ScriptSDK/include` and `build/ScriptSDK/lib`.
The SDK contains public Bazzalt/EnTT headers, the shared engine, and its Windows
import library. The editor extension depends on the engine DLL beside it.
Production packaging includes both the DLL and the SDK with each editor version.
Script compilation automatically locates this SDK, includes its headers, and
links the engine library. No manual engine linking is needed in gameplay files.
Missing SDKs produce a repair/rebuild diagnostic, not a partially linked module.

C++ modules must be rebuilt against their editor version's SDK and compatible
compiler/standard library. On Windows, compilation uses the MSVC ABI, release
STL layout, and dynamic CRT to match the native runtime. Arbitrary C++ ABI
compatibility across compilers/editor versions is not promised. The incremental
cache includes all SDK headers and the link library, so API changes invalidate
compiled modules. Existing optional V1 service exports remain accepted.

## ECS and module lifetime

Built-in component hashes are explicit, compiler-independent EnTT IDs so a
Clang gameplay module and MSVC engine access the same component pools.
User-defined components use the shared script compiler's ordinary EnTT IDs.
Define shared custom types in a common header and compile modules with the same
SDK/toolchain. Engine systems are not exported as public loop controls.

During lifecycle callbacks, the runtime tracks the executing module. Scenes
retain that module when script code accesses mutable ECS storage or adds systems.
Script-created scenes and component serialization registries retain it too.
Stopping scripts destroys behavior instances immediately, but a DLL is unloaded
only after all scene-owned component pools, systems, and registered callbacks
that reference its code have been destroyed. This prevents dangling vtables and
destructors. Static initialization and user-created worker threads must not
modify scenes; gameplay ECS access is main-thread only.

`BazzaltPublicScriptApiTests` loads a real module and verifies host-scene identity,
direct transform edits, world transforms under parents, built-in component
access, shared time, custom component/system creation, and safe delayed unload.
