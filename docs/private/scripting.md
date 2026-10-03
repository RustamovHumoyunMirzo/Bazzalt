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

## Component identity and validation

Each source file declares exactly one `COMPONENT(TypeName)`. Type names must be
unique across project script files; renaming the file alone does not rename the
component. For example, `code0.cpp` and `code1.cpp` cannot both declare
`COMPONENT(MyComp)`. Rename one declaration to a distinct type. Both conflicting
sources are excluded from Add Component until resolved, with their paths reported
in Console. Play validates project declarations before compiling attached sources.

Each property name must be unique within its component. Comments, string literals
containing macro examples, and properties outside the component body are excluded
from discovery. Add Component disables types already attached to the active entity;
drag/drop and manifest attachment also reject repeated types or sources. Fields
belong to one descriptor and are never merged by display name.

Older manifests containing duplicates display only their first entry, without
combining fields. The stored data is retained; runtime binding rejects duplicates.
To repair these entries, remove that script component (removing all duplicate
entries of the same type), resolve the conflicting declarations, and reattach the
desired script. The native loader independently rejects duplicate entity/type
bindings and one type being supplied by multiple modules.

Compiled modules expose a versioned C ABI. The private native runtime validates that ABI, creates one behavior instance per enabled attachment, applies serialized inspector properties, and calls `OnCreate`, `OnUpdate`, and `OnDestroy` with deterministic reverse-order teardown. Modules are unloaded on Stop; exceptions from gameplay callbacks are isolated from the editor loop.

## Toolchain ownership

End users do not need a system compiler. Editor distributions carry a pinned private LLVM/Clang installation at `toolchain/llvm`. Engine developers install it with `scripts/get_llvm.ps1` on Windows or `scripts/get_llvm.sh` on supported POSIX hosts. Production packaging fails if that toolchain is absent, preventing a broken editor release.

`Behavior` exposes lifecycle callbacks only. It intentionally does not expose engine initialization, event pumping, rendering presentation, or shutdown.
