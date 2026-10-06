# Lua scripting implementation and contribution guide

## Architecture and dependencies

Lua 5.4.9 is built as `BazzaltLua`, a separate shared library named
`bazzalt_lua.dll` on Windows. It is not statically embedded into Bazzalt.dll.
Ship it in the same directory as Bazzalt.dll, both in the editor's private
native module directory and in a game's runtime directory. Core SDK packages
include both libraries in `lib`. The Hub does not need or ship the Lua VM.
Sol2 3.3.1 is header-only binding infrastructure compiled through the private
`BazzaltLuaBindings` static implementation target and linked into the core.
Its template internals are excluded from Windows automatic DLL exports; no
Sol2 DLL, compiler or development SDK is required to run Lua behaviors.

Run `scripts/get_lua.ps1` and `scripts/get_sol2.ps1` on Windows, or the `.sh`
equivalents on POSIX, before configuring CMake. Lua's official archive SHA-256
and Sol2's peeled release commit are pinned. Lua uses its MIT-style license;
preserve its upstream license notice with redistributed binaries. The source
archive and reference manual are at [Lua.org](https://www.lua.org/ftp/) and
[Lua 5.4 manual](https://www.lua.org/manual/5.4/manual.html).

## Import, caching, and Play

`.lua` is a UUID asset with the usual YAML `.meta`. `LuaBytecode` importer
version 50409 parses source without executing it and writes `.blua` cache
artifacts. Syntax/import errors fail normally and keep the asset unready.
Lua syntax failures are retained as failed assets with LastError, allowing the
project/editor to open; they block execution instead of falling back to stale
bytecode. Attached source files have a debounced watcher that imports only the
changed Lua files, without rescanning models. Play waits for pending imports;
changes made during Play are imported after Stop, not hot-reloaded mid-callback.
Raw-import metadata from older projects upgrades to the specialized importer.
Runtime loading accepts binary chunks only, not source scripts.

Source-free game exports can place the imported `.blua` in their Assets tree and
retain the original asset UUID in its matching `.meta`. The database accepts
these bytecode assets without parsing source; serialized scene attachments keep
resolving the same UUID. Bytecode compatibility is checked when the VM loads it.

Lua is interpreted, but it still parses source into VM bytecode. That step
belongs to asset import, not C++ compilation or the Play compile dialog.
Lua-only Play never invokes Clang/MSVC. Mixed projects still compile their C++
behaviors. Bytecode is VM-version/number-representation dependent: rebuild it
when the Lua version or target ABI changes; do not treat it as a universal or
untrusted interchange format. Ship the imported cache along with asset metadata
and game scenes. The core does not embed project scripts as engine resources.

Scene `ScriptComponents` store Lua attachments and property overrides using
asset UUIDs. The editor's legacy attachment manifest remains its discovery/UI
store; synchronization writes the game-facing scene data. Serialized Lua asset
UUIDs survive renaming and do not encode a developer's absolute path.

## Authoring behaviors

Create a Lua Behavior in Asset Browser, or add a `.lua` file in Assets. Use the
provided `icons/abrowser/lua.svg`. Add it from Add Component or drag it onto an
entity. Source inspection only reads declaration comments, never runs code:

```lua
-- COMPONENT(Walk)
-- PROPERTY(float, Speed, 4.0)
local B = Bazzalt
local Walk = { Speed = 4.0 }
function Walk:OnUpdate(deltaTime)
    local transform = self.Entity:GetWorldTransform()
    transform.Position = transform.Position + B.Vec3.new(self.Speed * deltaTime, 0, 0)
    self.Entity:SetWorldTransform(transform)
end
return Walk
```

Each behavior returns a table. `COMPONENT` is optional and defaults to the file
stem; an explicit name must be unique across Lua and C++ behaviors. Duplicate
properties and duplicate attachments are rejected. The runtime supplies
`self.Entity` and applies inspector overrides before OnCreate. Optional methods
are OnCreate, OnUpdate(deltaTime), OnFixedUpdate(fixedDeltaTime), OnDestroy.
Each attachment has an isolated VM and private table state. Disabled attachments
do not update. Stop destroys runtime state, leaving authoring values unchanged.
See `examples/Walk.lua` for actual WASD movement through Input and world transforms.

## Gameplay API and value semantics

`Bazzalt` contains Vec2/Vec3/Vec4, Mat3/Mat4, Quaternion, UUID, Time, Input,
KeyCode/MouseButton/InputAxis and rendering/query enums. Constructors use
`.new(...)`; static functions use dots, object methods use colons. Vectors and
matrices support arithmetic; matrix Get/Set use checked zero-based coordinates.
Out-parameter APIs such as UUID.TryParse and Mat4.TryInverse return multiple Lua
values instead. Containers exposed as result lists use one-based Lua tables.

SceneManager exposes active-scene access and deferred scene loading, not engine
initialization or frame control. Scenes expose entity creation/destruction,
hierarchy, world transforms, model instantiation, environment, Pick/PickAll,
Raycast/RaycastAll and overlaps. GetEntities optionally accepts a component name.

Entity's generic component operations accept a type-name string: AddComponent,
HasComponent, GetComponent, TryGetComponent, SetComponent, RemoveComponent,
IsComponentEnabled, SetComponentEnabled. All built-in component data is bound,
including camera post processing, DoF, primitives, blur, vignette and model nodes.
GetComponent returns a **copy**, not an unsafe pointer to registry storage:

```lua
local light = self.Entity:GetComponent("Light")
light.Range = 20
self.Entity:SetComponent("Light", light)
self.Entity:SetComponentEnabled("Light", false)
```

Default components cannot be removed or disabled. Identity and Hierarchy cannot
be overwritten; use GetUUID/SetParent/RemoveParent to preserve ECS invariants.
Material, Shader, AssetManager, model descriptors, environment, and custom
post-processing parameters/stacks map to their C++ gameplay counterparts.
PostProcessingStack.SetEffect explicitly commits edited effect copies.

C++ templates/traits, raw EnTT registries, native-module ABI exports and pointers
are not Lua APIs. Project loading/saving and editor loop control remain host-only.
Lua behaviors are user-defined components with their own fields/lifecycle; they
do not declare a new C++ component layout. Adding a new native component still
requires a native binding and operation registration before Lua can access it.

## Lua systems

`scene:AddSystem(name, table)` registers a Lua system with optional
OnCreate(scene), OnUpdate(scene, dt), OnFixedUpdate(scene, dt), OnDestroy(scene).
HasSystem/GetSystem/RemoveSystem and SetSystemEnabled/IsSystemEnabled use that
name. Implement component iteration with `scene:GetEntities("Light")`, then
GetComponent/SetComponent. Runtime-owned dispatch runs through the normal Scene
system lifecycle; scripts never call the engine loop. Adding/removing systems
inside system updates is rejected. Stop removes Lua system callbacks before VM
destruction. System names must be unique across behaviors in the active scene.

## Error and trust boundaries

Only base/math/string/table/utf8 standard libraries are opened. OS, I/O, package,
debug, native module loading, loadfile/dofile/load are absent. Each VM has a
32 MiB allocation ceiling; source/bytecode assets are limited to 4 MiB and calls
have an instruction budget. Failed updates disable the offending behavior or
system and report the error; initialization errors fail Play. These are defensive
limits, not a security guarantee for hostile bytecode or a process sandbox.
Only execute bytecode produced by the trusted project asset importer.

## Adding or changing public APIs

1. Implement the C++ API without exposing private engine/editor internals.
2. Add the Lua binding in LuaMath, LuaComponents, LuaScene, LuaAssets or LuaServices.
3. For a native component, register its fields and ComponentOperations; return
   copies and validate mutations rather than returning dangling ECS pointers.
4. Adapt overloads, optional arguments, out parameters and containers explicitly.
5. Add execution assertions to LuaRuntimeTests and editor discovery/import tests.
6. Update this document/examples. Keep PascalCase names consistent.
7. Run CTest plus Editor.tests.test_lua_scripting. Release checks must verify that
   the separate Lua DLL is copied and has the same architecture as the core.

Do not add a new C++ gameplay API without its Lua equivalent or an explicit,
documented host-only/compile-time exclusion. API parity is a contribution rule,
not an automatic C++ reflection system.
