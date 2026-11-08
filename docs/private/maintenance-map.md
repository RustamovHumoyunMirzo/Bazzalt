# Code map and sources of truth

Use this map before adding another service or registry. Follow an existing
feature through its entry points and extend the owner that already serves it.

## Repository boundaries

| Location | Responsibility |
| --- | --- |
| `include/Bazzalt` | Public C++ contracts and serializable component values |
| `src/Runtime`, `src/Engine` | Core adapters, service lifetime, loading and orchestration |
| `src/Rendering` | Filament synchronization, GPU resources and viewport passes |
| `src/Physics` | Core physics adapter plus Jolt provider |
| `src/Navigation`, `src/AI` | Navigation/behavior adapters and implementations |
| `src/GUI` | Game GUI layout, paint, fonts and styles |
| `src/Script` | Lua exposure of public gameplay APIs |
| `src/Serialization` | Component codecs and scene persistence |
| `src/EditorBridge` | Native editor inspection and commands |
| `Editor` | Qt authoring, selection/history and localized UI |
| `Compiler` | Content compilation, packaging and native game hosts |
| `Launcher` | Hub, installed editor versions and project launching |
| `cmake/modules` | Native provider declarations and dependency selection |
| `scripts` | Dependency fetching, development and release tooling |
| `releases/versions.json` | Product versions |
| `docs/public` | Versioned user documentation |
| `docs/private` | Contributor implementation knowledge |

## Find the integration point

| Task | Start here | Follow through |
| --- | --- | --- |
| Terrain authoring | `include/Bazzalt/Terrain.h`, `src/Terrain` | TerrainBridge, terrain overlay, collision source collection and LuaTerrain |
| Component fields | Public `Components/*.h`, feature `*Fields.h` | Serializer, native inspector adapter, Lua bindings |
| Component serialization | `SceneSerializer` in `src/Serialization/Serialization.cpp` | Feature registration helpers and `AdvancedLODSerialization.cpp` |
| Inspector operations | `src/EditorBridge/PythonModule.cpp`, feature bridge headers | `Editor/gui/controller.py`, shared field widgets |
| Field sections/ranges/pickers | Native inspection descriptors | `Editor/gui/inspector_sections.py`, `Editor/property_rules.py` |
| Script properties | `include/Bazzalt/Script.h`, `Editor/scripting.py` | Property rules, native/Lua script runtimes |
| Lua registration | `src/Script/LuaBindings.h`, `LuaServices.cpp` | Feature `Lua*.cpp`, entity operations and teardown |
| Module loading | `src/Runtime/ModuleLoader.*` | Feature `*ModuleApi.h` and core adapter |
| Module builds/files | `cmake/modules/Builtins.cmake` | Provider, generated `runtime-libraries.json`, staging |
| Asset import | `src/Runtime/AssetDatabase.cpp` | `AssetDependencies.inl`, editor classification, cooking |
| Scene lifecycle | `src/Scene/Scene.cpp`, `src/Engine/Engine.cpp` | System hooks, scene-release functions, script runtimes |
| Selection/history | `Editor/history.py`, `Editor/gui/controller.py` | Feature controller/panel |
| Settings | `bazzalt/settings.py`, `Compiler/settings.py` | `Editor/gui/preferences.py`, build/project dialogs, platform consumers |
| Panel shell | `Editor/gui/application.py`, `Editor/gui/panels` | Dock registration, resources/locales, persisted IDs |
| Runtime packages | `Compiler/build.py`, `scripts/stage_game_runtime.py` | Release architecture audit, Hub validation |

Some older files group unrelated features. `AdvancedLODSerialization.cpp` also
registers behavior, animation and navigation data. Reuse the existing field
visitor. Move a feature's registration into its own function when substantial
work warrants that change. Preserve registration order, type IDs and old-format
handling during the move.

## Trace an existing vertical slice

`BehaviorTreeAgent` is a compact example crossing the layers.

1. `include/Bazzalt/Components/BehaviorTreeAgent.h` defines persisted values.
2. `src/AI/BehaviorFields.h` visits fields for codecs and inspection.
3. `src/Serialization/AdvancedLODSerialization.cpp` registers the stable type ID.
4. `src/EditorBridge/LODBridge.h` supplies inspection/edit dispatch.
5. `src/Script/LuaAI.cpp` supplies bindings and callback cleanup.
6. `src/AI/BehaviorTreeSystem.cpp` owns registrations and native module leases.
7. `src/AI/BehaviorAgentRuntime.cpp` implements the provider-side execution cache.
8. `tests/BehaviorTreeTests.cpp` and `tests/LuaAITests.cpp` verify the contracts.
9. `Editor/tests/test_behavior_tree.py` and compiler tests verify authoring/cooking.

These layers have different lifetimes. Reusing a field visitor avoids copying
field names while keeping Lua handles, GPU objects and Qt widgets in their
respective owners.

## Useful searches

```powershell
rg "VisitBehaviorTreeAgent|BehaviorTreeAgent" include src Editor Compiler tests
rg "Register.*Components|RegisterDescriptor" src/Serialization src/GUI src/Physics
rg "BindLua|UnbindLua" src/Script src/Runtime/LuaRuntime.cpp
rg "AddSystem|Release.*Scene" src/Scene src/Engine
rg "runtime-libraries|LibraryNames" cmake scripts bazzalt
```

The [verification guide](verification.md) lists generators and checks. Keep
caches, binaries and dependency checkouts separate from authored changes.
