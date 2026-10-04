# BAZZALT private documentation

This handbook documents the engine as it exists in the repository. It explains
the public game-facing API, private editor/runtime boundary, persistent formats,
resource ownership, and extension points for game code.

The standalone [native Hub](native-hub.md) manages projects and installed editor
versions using platform-native Qt Widgets, independently of the editor UI.

## Read in this order

1. [Getting started](getting-started.md) — dependencies, building, linking, and
   a first component/system.
2. [Architecture and ownership](architecture.md) — the editor-first model and
   why game code does not control the engine loop.
3. [ECS, components, and systems](ecs.md) — entities, custom components,
   queries, systems, and lifecycle rules.
4. [Scenes, hierarchy, and serialization](scenes-and-serialization.md) — UUIDs,
   parenting, scene transitions, YAML formats, and custom serializers.
5. [Asset database and import pipeline](assets.md) — `.meta` files, cache
   invalidation, importers, and runtime lookup.
6. [Rendering](rendering.md) — cameras, lights, meshes, glTF, materials, and the
   private Filament backend.
   [Scene environments](environments.md) covers HDR/EXR imports, scene lighting,
   environment materials, and shared inspector previews.
   [Materials and shaders](materials-and-shaders.md) covers the material Inspector,
   demand-driven compilation, typed C++ handles, and script asset properties.
7. [Math](math.md) — coordinate conventions and public math types.
8. [Public API reference](api-reference.md) — header-by-header reference.
   [Gameplay time](time.md) covers slow motion, scaled/unscaled clocks, pause,
   and fixed updates.
   [Gameplay input](input.md) covers keyboard/mouse APIs, focus gating,
   Game maximization, and Play-mode restoration.
   [Native script SDK](script-sdk.md) covers direct public API linking, shared
   engine state, entity properties, and safe native module lifetime.
9. [Roadmap](roadmap.md) — planned systems that are not part of the current API.

## API stability boundary

Only files under `include/Bazzalt` are public. The following are private even
when tests or the editor include them:

- `src/Runtime` — engine lifetime, project/scene persistence, imports, frames.
- `src/Rendering` — Filament objects, gltfio, filameshio, and GPU resources.

Applications may depend on public C++ types and serialized UUIDs. They should
not persist EnTT entity identifiers, pointers, cache paths, or renderer handles.
