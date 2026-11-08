# Native module development and replacement

## Contract and ownership

The core owns scene identity, public values, serialization orchestration and host
lifecycle. A provider implements a private service through a versioned dispatch
table. Physics, AI, GUI and networking have separate shared-library providers.
Lua and crypto are dependency libraries with their own CMake configuration.

Public methods call core adapters. Adapters resolve the sibling module, validate
its table, and forward operations. Providers can change without changing game
method calls or persisted component IDs.

Tables use C++ types and require matching compiler, architecture, standard library
and configuration. Their entry point has C linkage. This is a same-build private
ABI. An interface-compatible replacement must also preserve behavior, ownership,
ordering, errors and data compatibility.

## Where to edit

| Concern | File |
| --- | --- |
| Default providers and implementation sources | `cmake/modules/Builtins.cmake` |
| Shared build/staging policy | `cmake/RuntimeModules.cmake` |
| Provider-specific dependencies/options | `cmake/modules/Physics.cmake`, `AI.cmake`, `GUI.cmake`, `Network.cmake` |
| Portable core-relative loading | `src/Runtime/ModuleLoader.h` and `.cpp` |
| Dispatch contract | Feature `*ModuleApi.h` |
| Forwarding and validation | Feature core adapter such as `src/Physics/PhysicsAccess.cpp` |
| Built library inventory | Generated `runtime-libraries.json` beside the core |
| Package inventory reader | `bazzalt/runtime_libraries.py` |

Default and replacement implementation sources are excluded from the core's
recursive inventory. A source cannot belong to two registered providers. This
prevents backend code from silently compiling into both the core and a provider.
Ownership uses canonical paths, including Windows short-path aliases and source
symlinks. Keep the core inventory and provider source keys normalized together.

## Add implementation code

Add the source to the existing declaration in `Builtins.cmake`. Keep the public
adapter outside that implementation list. The provider gets C++20, private
includes, PIC, runtime search paths, MSVC UTF-8 handling and SDK staging. Add
dependency-specific options with ordinary `target_*` commands.

The helper leaves allocation, worker scheduling, SIMD, rendering/physics commands
and dispatch signatures under the module's control.

Build `BazzaltRuntime` for the complete core and registered sibling-library set.
Building `Bazzalt` alone builds the core's link dependencies. Keeping providers
out of that build dependency permits providers to link back to public core
operations without a circular target dependency. Hosts/tests use the full set.

## Replace a provider in a studio fork

Create an external `StudioPhysics.cmake`.

```cmake
find_package(StudioSolver CONFIG REQUIRED)
bazzalt_add_runtime_module(${BAZZALT_MODULE_TARGET}
    OUTPUT_NAME ${BAZZALT_MODULE_OUTPUT_NAME}
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/StudioPhysics.cpp"
    LIBRARIES Bazzalt StudioSolver::Solver)
target_compile_definitions(${BAZZALT_MODULE_TARGET} PRIVATE STUDIO_SOLVER=1)
```

Select it in a separate build directory.

```powershell
cmake -S . -B build-studio -DBAZZALT_PHYSICS_PROVIDER=C:/Studio/Physics/StudioPhysics.cmake
cmake --build build-studio --config Release --target BazzaltPhysics
```

Selectors also exist for `BAZZALT_AI_PROVIDER`, `BAZZALT_GUI_PROVIDER` and
`BAZZALT_NETWORK_PROVIDER`. Use absolute paths for external sources. Selection
occurs at configure time. Physics/AI providers own solver dependency setup, so
alternatives bypass default Jolt/Recast configuration. Networking's crypto
dependency is also used by core package/save services and remains configured by
`cmake/Networking.cmake`.

Implement every required dispatch operation. Preserve the supplied target and
output basename. Entry symbol, version/size and required function checks stay in
effect. Run existing feature tests against the replacement and add backend-specific
tests. Configuration checks identity and source ownership. Behavioral equivalence
requires execution tests.

## Add a native module

1. Define public values/operations under `include/Bazzalt`.
2. Define the private table and core adapter in the feature directory. Use
   `BehaviorModuleApi.h` as an existing opaque-handle example.
3. Add the provider CMake file and a declaration in `Builtins.cmake`.
4. Export an entry that checks requested version and table size. The adapter uses
   `LoadSiblingModuleApi<Api>` and validates each required function pointer.
5. Integrate at the actual lifecycle boundary. Scene caches need scene-release
   paths. Callback registries disconnect owners before objects/VMs/DLLs disappear.
6. Add Lua, persistence and tools using the [change recipes](contributor-workflows.md).
7. Test errors, scene clear, stop/restart, reload and real host use.

Registered providers join host/test dependencies and Windows DLL staging. CMake
emits filenames into `runtime-libraries.json`. Windows/Linux/macOS release and
game runtime template staging consume that inventory. Older build directories
fall back to the previous built-in library list. Reconfigure before packaging.

The compiler's `runtime.json` continues to record and validate final files.
Platform launcher libraries, CRT/SDL dependencies, tools, licenses and installer
policy have separate owners. Review those when deployment requirements change.

## Loading and lifetime

`LoadSiblingModuleSymbol` accepts an ASCII library basename and entry identifier.
It derives the platform filename, resolves it relative to the core, and reports
load/entry errors. Windows paths use native Unicode handling. Project/current
directories are outside this implementation lookup.

Loaded libraries and their cache remain resident through shutdown because tables,
serializers and opaque objects can retain code. Scene resources still need their
own release operations. This seam provides configure-time replacement. Gameplay
hot reload uses separate module leases and owner cleanup.

Avoid work in library static constructors. Initialize through table operations
after loading, and keep load-time initialization free of calls into module loaders.

## Change or remove a contract

Bump a changed private table version, update core/provider checks together and
rebuild the runtime set. Preserve old authored data with serializer migrations.
Update SDK/package ABI metadata when that contract changes.

For removal, handle existing serialized records, scripts and runtime users first.
Stop creating the feature, disconnect callbacks and release scene resources.
Remove provider/dependency declarations after migrating callers. A core-used
module remains required until those callers have explicit replacement behavior.
