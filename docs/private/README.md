# Contributor handbook

This is the actively maintained implementation handbook for engine, editor,
compiler and Hub contributors. The directory name `private` describes internal
APIs. These guides are part of the repository's contribution contract.

Start with the task you have. Keep the relevant guide accurate in the same pull
request as its implementation. Gameplay usage and callable reference remain in
[the public documentation](../public/README.md).

## First contribution

1. [Development setup](getting-started.md) explains source-mode editing and SDK builds.
2. [Architecture](architecture.md) establishes ownership and dependency direction.
3. [Code map](maintenance-map.md) identifies the source of truth for each subsystem.
4. [Change recipes](contributor-workflows.md) walks through fields, assets and panels.
5. [Module providers](module-development.md) explains adding/replacing native implementations.
6. [Verification](verification.md) selects checks and documents platform coverage.

## Runtime and data

| Area | Implementation guides |
| --- | --- |
| Entity state and math | [ECS](ecs.md), [math](math.md), [scene queries](scene-queries.md) |
| Persistence | [Scenes/serialization](scenes-and-serialization.md), [prefabs](prefabs.md), [networking/save games](networking-and-save-game.md) |
| Assets | [Import pipeline](assets.md), [dependencies](asset-dependencies.md), [model hierarchies](model-hierarchies.md) |
| Execution | [Time](time.md), [input](input.md), [jobs/profiling](jobs-profiler.md), [optimization work](optimizations-0.8.md) |
| Physics and AI | [Jolt](physics.md), [replay](physics-replay.md), [navigation](navigation.md), [behavior trees](behavior-trees.md) |
| Animation and worlds | [Animation](animation.md), [cinematics](cinematics.md), [streaming/instancing](streaming-instancing.md) |
| Audio | [Audio runtime](audio.md), [mixer effects](audio-mixer-effects.md) |
| Diagnostics | [Developer console](developer-console.md), [scripting console](scripting-console.md) |

## Rendering and GUI

| Area | Implementation guides |
| --- | --- |
| Backend and drawing | [Rendering](rendering.md), [graphics API](graphics.md), [advanced LOD](advanced-lod.md) |
| Materials | [Materials/shaders](materials-and-shaders.md), [runtime materials](runtime-materials.md), [shader graph](shader-graph.md) |
| Lighting and VFX | [Environments](environments.md), [baked lighting](baked-lighting.md), [VFX](vfx.md) |
| GUI runtime | [GUI](gui.md), [CSS](gui-css.md), [styles/fonts](gui-styles-fonts.md), [CSS motion](gui-css-motion.md) |

## Scripting and extensibility

| Area | Implementation guides |
| --- | --- |
| Gameplay scripts | [Scripting](scripting.md), [native SDK](script-sdk.md), [Lua bindings](lua-scripting.md), [hot reload](hot-reload.md) |
| Global lifecycle | [Application events](application-events.md), [global events](global-events.md), [global subsystems](global-subsystems.md) |
| Packages and reference | [Extensions](extensions.md), [API overview](api-reference.md), [reference maintenance](api-reference-maintenance.md) |

## Editor and distribution

| Area | Implementation guides |
| --- | --- |
| Editor shell | [Panels](editor-panels.md), [widgets](editor-widgets.md), [services](editor-services.md), [runtime bridge](editor-runtime-integration.md) |
| Scene authoring | [Toolbar/gizmos](editor-toolbar-and-gizmos.md), [transform feedback](editor-transform-feedback.md), [camera persistence](scene-camera-state.md), [tools](editor-tools.md) |
| Branding and collaboration | [Branding](branding.md), [version control](version-control.md) |
| Hub | [Native Hub](native-hub.md), [Hub/editor storage](hub-and-editor-data.md) |
| Game output | [Compilation](game-compilation.md), [security](game-build-security.md), [build contracts/limits](game-build-limitations.md), [cross/remote builds](cross-and-remote-builds.md) |
| Releases | [Production packaging](production-packaging.md), [Linux](linux-platform.md), [macOS](macos.md), [macOS runner capacity](macos-runner-capacity.md), [Android](android.md) |
| Planned work | [Roadmap](roadmap.md) |

## Keeping this handbook useful

Document where a feature enters the engine, who owns its resources, how its data
is persisted, and which tests exercise it. Explain replacement contracts and
compatibility with older authored data. Record limitations at the point where a
contributor will encounter them.

Run `python scripts/check_contributor_docs.py` to check links and index coverage.
The documentation workflow runs this check for contributor-guide changes.
