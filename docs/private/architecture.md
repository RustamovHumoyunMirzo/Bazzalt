# Architecture and ownership

## Dependency direction

```text
Game code                 Editor / Hub             Native game host
    |                          |                          |
include/Bazzalt          Python runtime bridge            |
    |                          |                          |
    +------------------- core runtime -------------------+
                              |
             public-value adapters and dispatch tables
                              |
        Physics / AI / GUI / networking implementations
```

`include/Bazzalt` defines gameplay values and operations. It must not require Qt
or include private solver/renderer headers. `src/EditorBridge` adapts runtime
operations for tools. The compiler cooks and packages content against a supplied
runtime. Final games use native hosts and have no Python/editor requirement.

## Lifetime owners

| Owner | Owns |
| --- | --- |
| Host | Window/surface, application startup and final shutdown |
| `Runtime::Engine` | Active project, loaded scenes, service instances, timing and transitions |
| `Scene` | Entity registry, stable UUID identity, systems and scene settings |
| Provider/service | Backend handles, per-scene caches, worker/GPU/solver resources |
| Gameplay script | Its state and registrations associated with its owner |
| Editor | Selection, history, authoring dialogs, layouts, source tools |
| Hub | Installed products, project discovery and editor launching |

The editor and standalone host use the same runtime contracts. Game scripts use
Application controls, scene operations and lifecycle hooks. Engine construction,
project saving and frame-loop implementation stay private.

Only `include/Bazzalt` is the public include boundary. `src/Runtime`,
`src/Rendering`, provider headers and editor bridge internals remain private even
when tests include them. `Entity::Id` is transient. Persist UUIDs, component values
and asset references.

## Frame and scene contracts

Read `Engine::Tick` and the scene/system methods before changing ordering. The
engine coordinates pending loads, input, scripts, fixed simulation, scene systems,
render synchronization and presentation. Audio, physics, input and global
subsystem phases have explicit event ordering. Details are in
[global events](global-events.md) and [subsystems](global-subsystems.md).

Scene loading requests are applied at safe boundaries. Stop callbacks and release
scene caches before tearing down referenced services and entity storage. Release
render resources while their backend is alive. Native gameplay module leases
must outlive callbacks, serializer functions and script objects using that code.

`Scene::Clear` retains the Scene object while replacing entity contents. Caches
must clear there as well as on destruction. An entity recreated with the same
UUID is a new generation. Compare the stored Entity handle, not just its UUID.

## Native implementation modules

Physics, AI, GUI and networking are sibling shared libraries behind private
version/size checked tables. CMake providers select their implementations. The
[provider guide](module-development.md) covers dependency isolation, lifetime,
replacement and packaging. C++ types cross these tables, so core/provider builds
must match their toolchain and architecture.

Filament also has core-owned rendering integration. Replacing it requires adapting
that resource/render contract. A provider boundary alone does not decouple all
renderer-specific code.

## Threads and work queues

Scene mutation and system lifecycle run on their owning thread unless an API
explicitly promises otherwise. Use the existing job system for background work,
keep input/output snapshots bounded and immutable where possible, and apply
results at the owner boundary. Do not let jobs capture destroyed scenes or code
from an unloaded gameplay module. See [jobs/profiler](jobs-profiler.md),
[streaming](streaming-instancing.md) and [hot reload](hot-reload.md).

## Authored data and tooling

Serialization IDs, property meanings, asset UUIDs and file formats are compatibility
contracts. Caches and import outputs are rebuildable. Version/migrate authored
formats and invalidate importer caches when their interpretation changes.
Inspector constraints complement runtime validation. Read-only UI refresh must
preserve selection and dirty state.

Use the [code map](maintenance-map.md) to find entry points and the
[change recipes](contributor-workflows.md) to maintain a complete vertical slice.
