# Architecture and ownership

## Editor-first runtime model

BAZZALT separates authoring from execution without creating two engines. The
editor and shipped game host the same runtime core; the editor adds tools and UI
around it. Game modules contribute data components, systems, and serializers.

The host owns:

- engine construction and shutdown;
- project opening/saving and scene saving;
- active-scene lifetime and frame timing;
- renderer initialization and GPU resource destruction;
- asset scanning, importing, and cache maintenance.

Game code owns:

- custom component and system definitions;
- behavior executed through lifecycle hooks;
- requests to load a scene at a safe boundary;
- read-only asset queries by UUID or source path.

## Layers

```text
Game/editor extensions
        |
        v
include/Bazzalt             Public renderer-independent API
        |
        v
src/Runtime                 Private orchestration and persistence
        |
        +--> src/Rendering  Private Filament integration
        +--> EnTT           ECS storage
        +--> rapidyaml      YAML parsing
```

Filament, gltfio, filameshio, cache artifacts, and EnTT handles are not stable
persistence APIs. `Entity::Id` values are transient; use `UUID` for identity
that must survive serialization or reload.

## Frame lifecycle

The private host conceptually:

1. Applies a deferred scene-load request.
2. Calculates delta time.
3. Dispatches enabled systems in registration order.
4. Lets private rendering systems synchronize ECS state to Filament.
5. Presents or embeds output through the owning application.

There is no public engine initialization, frame update, or overridable
application loop. This prevents game code from creating a second renderer,
loading a project during iteration, or destroying resources out of order.

## Scene transitions and ownership

`SceneManager::LoadScene` queues a transition. The host applies it at the next
safe frame boundary, so it can be called from `OnUpdate` without destroying the
scene currently being iterated.

The render backend owns the Filament engine, renderer, scene, gltfio loaders,
texture providers, materials, buffers, and render entities. ECS components hold
only serializable settings and asset UUIDs. Destruction happens in reverse
dependency order before the Filament engine is released.

## Threading

Scene mutation and system lifecycle are foreground-thread APIs. Filament's stb
provider may decode on worker threads, but the private renderer services its
queue. Do not mutate a scene concurrently unless a future API explicitly marks
an operation thread-safe.
