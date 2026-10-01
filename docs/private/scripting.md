# C++ behaviors

BAZZALT behaviors are public gameplay components, while compilation, module loading, and the application loop remain editor/runtime responsibilities.

```cpp
#include <Bazzalt/Script.h>

COMPONENT(PlayerMovement) {
public:
    PROPERTY(float, Speed, 6.0f)
    PROPERTY(bool, AllowInput, true)

    void OnCreate() override {}
    void OnUpdate(float deltaTime) override { (void)deltaTime; }
    void OnDestroy() override {}
};
```

Create a C++ component from the Asset Browser, then drag it onto an entity or select it under **Add Component > Scripts**. `PROPERTY` declarations become editable inspector fields. Attachments use entity UUIDs and are stored below the project's `.bazzalt` directory; generated binaries and incremental state live in `.bazzalt/ScriptAssemblies` and should not be committed.

Pressing Play builds only enabled, attached scripts. Compiler errors and warnings appear as itemized Console messages with the `Compiler` source. A failed build does not enter Play mode.

Compiled modules expose a versioned C ABI. The private native runtime validates that ABI, creates one behavior instance per enabled attachment, applies serialized inspector properties, and calls `OnCreate`, `OnUpdate`, and `OnDestroy` with deterministic reverse-order teardown. Modules are unloaded on Stop; exceptions from gameplay callbacks are isolated from the editor loop.

## Toolchain ownership

End users do not need a system compiler. Editor distributions carry a pinned private LLVM/Clang installation at `toolchain/llvm`. Engine developers install it with `scripts/get_llvm.ps1` on Windows or `scripts/get_llvm.sh` on supported POSIX hosts. Production packaging fails if that toolchain is absent, preventing a broken editor release.

`Behavior` exposes lifecycle callbacks only. It intentionally does not expose engine initialization, event pumping, rendering presentation, or shutdown.
