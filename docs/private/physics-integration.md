# Jolt physics integration preparation

Status: dependency/build preparation only. No simulation, Rigidbody/Collider
components, physics queries, editor tools, C++ or Lua physics APIs exist yet.
The current Scene.Raycast/Pick/Overlap methods remain geometry-based scene
queries. They must not silently become physics queries when Jolt is added.

## Prepared dependency boundary

The single source pin is `scripts/dependencies/jolt.json`: Jolt 5.3.0 at
`0373ec0dd762e4bc2f6acdb08371ee84fa23c6db`. Both platform shell wrappers invoke
`scripts/get_jolt.py`, which fetches source only and checks the revision and
working tree. CMake never downloads dependencies. Leave
`BAZZALT_PREPARE_JOLT=OFF` for normal builds; missing Jolt does not affect them.

The opt-in `cmake/Jolt.cmake` creates private `Jolt` and
`Bazzalt::JoltDependency` targets. It does not link either into Bazzalt or add
Jolt to ScriptSDK public include paths. CMake 3.20+ is required only for this
opt-in path. Build Jolt only in GitHub Actions, not on the developer machine.
The dedicated workflow builds a dependency-only registration/link/teardown
probe for Windows/Linux x64 and x86, independent of Filament, Qt and core.
Its success checks dependency readiness, not BAZZALT physics support.

Configuration: static PIC, single-precision positions, 16-bit object layers,
SSE2 baseline, no optional AVX/SSE4/FMA instructions, no global flag overrides,
no LTO, samples, upstream tests, profiler, object streams or debug renderer.
MSVC uses the DLL CRT to match core. Linux x64 uses Clang/libc++; Linux x86
uses Clang/libstdc++, matching existing SDKs. These settings are private ABI
choices; never mix libraries built with incompatible options. Broad upstream
option names are scoped to a function/subdirectory and must not change core,
Lua, GUI or scripts. Upstream sources must remain unmodified.

## Planned implementation ownership

Put adapters/worlds/body maps/jobs/listeners in `src/Physics`, with private
engine access in `src/Runtime`. Only engine-owned code may initialize Jolt.
One process-level service owns allocator/factory/type registration and a
bounded worker pool. One scene simulation session owns its own physics world,
temporary allocator, body map and deferred command/contact queues. Inactive
loaded scenes must not acquire running simulations merely by being in the
Hierarchy. Scene UUID + entity UUID identifies an object; Jolt BodyID is a
private generational handle and is never serialized or exposed to scripts.

Initialization must be exception-safe: register the process service once,
create scene resources only after loading data, join workers before destroying
worlds, release shapes/worlds before unregistering types/factory/allocator.
Do not make runtime depend on editor classes, Python, Qt or extension tools.
The future adapter can link the prepared dependency privately. Decide and test
its shared-library distribution separately before adding a shipping module.
No separate physics DLL is promised by this preparation change.

## Fixed-step and event insertion points

Use the existing `Time` clock and accumulator in `Engine::Tick`; do not add a
second scheduler, variable-delta solver update, or polling thread. The intended
order for each existing fixed tick is:

1. Application OnFixedUpdate, scene systems, native scripts and Lua scripts.
2. Apply deferred creation/removal/teleport/force requests and synchronize
   authored static and kinematic transforms into the world.
3. Step Jolt exactly once with this tick's scaled fixed delta.
4. Synchronize dynamic body poses back to scene world transforms.
5. Drain validated contact/trigger/activation notifications on the main thread.

The insertion point is after `m_luaRuntime->FixedUpdate(step)` and before the
fixed loop repeats. Keep the existing pause, time scale, catch-up limit and
exception/error behavior. Rendering reads interpolated poses using the residual
accumulator fraction; never write interpolated poses into authoritative bodies.
Fixed callbacks need finalized ordering tests before making physics public.

Jolt worker listeners may only enqueue bounded, copied event data. Do not call
Lua, user C++, mutate EnTT, or touch editor/UI objects from a physics worker.
Revalidate scene/entity generations before dispatch; drops/removals must not
leave callbacks referring to destroyed entities. Changes made by collision
callbacks apply at the next safe synchronization point. Define enter/stay/exit,
trigger vs contact, disabled-body and scene-unload behavior before release.

## ECS, transforms and lifetime

Preserve lightweight public component data; runtime handles belong to private
maps. Use world-space rigid poses and explicit conversion to/from Bazzalt's
right-handed Y-up, negative-Z-forward math. Jolt types never cross public
headers, Lua userdata or serialization. Use metres, seconds and kilograms as
the future API units. Quaternions remain normalized; no Euler conversion in
the solver loop. SetWorldTransform performs parent-local conversion after a
dynamic writeback. Do not silently inherit rigid bodies through ancestors.

Define policies for nested bodies, non-uniform/negative scale, singular parent
matrices, collider offsets and shape rebuilds before exposing authoring fields.
Reparenting that preserves world space must not teleport a body. Transform edits,
undo/redo, prefab/model instantiation, component enable/disable/removal and scene
replacement must invalidate or resynchronize private state. Destroy children
and their physics bindings before reusing entity IDs. Never keep pointers into
EnTT component storage across mutations or worker jobs.

Editing does not simulate. Play starts from authored scene data; Stop tears down
the simulation before restoring the editor snapshot. Pause does not advance;
Step advances one fixed tick. Scene unload/reload/project switch and failed Play
initialization must release all bodies/jobs/events before scene storage dies.
Exact engine methods to cover include StartScripts/StopScripts,
ProcessPendingSceneLoad, LoadScene/LoadProject and Release; no hooks are installed
yet because there is no backend to invoke.

## Next integration deliverables

Add stable, versioned component serializers and reflected inspector fields only
with working runtime behavior. Public C++ physics APIs must have matching Lua
bindings and reference docs. Keep physics queries in a separate Physics/scene
physics service with explicit layer/trigger/disabled filtering. Keep visual
scene queries unchanged. UUID references survive save/cook; private handles do
not. Add collision layers, shape cache/cooking format keys (Jolt version, target,
precision), input validation and cache invalidation before mesh colliders.
Do not use Jolt ObjectStream as the public scene format.

Gate release with gravity/contact/trigger/CCD/sleep tests, parented transforms,
component toggles, body reuse, destruction inside callbacks, scene switching,
Play/Stop/reload, pause/time scale/single stepping, failed initialization,
C++/Lua parity, and Windows/Linux x86/x64 standalone cooked-game tests. Test
repeatability with tolerances; do not promise cross-platform determinism merely
because a Jolt CMake switch exists. Editor collision guides and profiling must
use engine abstractions and stay out of game presentation.

## Dependency-only CI commands

On CI, fetch with `python scripts/get_jolt.py`, then configure
`cmake -S tests/jolt -B build/jolt-probe -DBAZZALT_PREPARE_JOLT=ON` with the
matching generator/compiler/architecture. Build that directory and run CTest.
Normal product workflows deliberately do not fetch, build or ship Jolt yet.

Upstream references: [build options](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/Build/CMakeLists.txt),
[platforms and CPU baseline](https://github.com/jrouwe/JoltPhysics/tree/v5.3.0#supported-platforms),
[initialization example](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/HelloWorld/HelloWorld.cpp).
