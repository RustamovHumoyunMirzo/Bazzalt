# Model assets and scene hierarchies

BAZZALT treats an imported model as one asset UUID. glTF and GLB nodes are descriptors inside
that asset, not independent assets. `AssetManager::LoadModel(assetId)` reads the cached model
and returns a `ModelAsset` containing its node names, stable index paths, local transforms,
mesh indices, child indices, and active-scene roots.

## Instantiation

Use `Scene::InstantiateModel(assetId, parent, name)` or pass an already loaded `ModelAsset`.
The result is a model-instance entity with `ModelInstance`; every source node becomes a child
entity with `ModelNode`. Nested source nodes retain their hierarchy. Mesh-bearing nodes also
receive `Mesh`, whose `ModelNodeIndex` selects only that glTF node from the shared model asset.
This creates a hierarchy such as:

```text
Human                     ModelInstance
├── Arm                   ModelNode
│   └── Hand              ModelNode, Mesh
└── Head                  ModelNode, Mesh
```

Stable paths use escaped node-name paths with deterministic duplicate-name suffixes; source
indices remain separate for rendering. Each entity has its own UUID and ordinary component storage. Adding a component to `Human`
does not add or copy it to Arm, Hand, or Head. Node metadata and model UUIDs are serialized in
`.bscene` files, and `StablePath` identifies the source node across scene save/load.

## Component inheritance

Component access is entity-local by default. This is important for predictable ECS queries and
matches prefab/model workflows in established editors. Transforms use
`ComponentInheritanceMode::Composed`: each node stores a local `Transform`, while
`GetWorldMatrix()` composes all ancestor transforms.

Data components that intentionally inherit may specialize the public trait:

```cpp
template<> struct Bazzalt::ComponentInheritance<MySettings> {
    static constexpr auto Mode = Bazzalt::ComponentInheritanceMode::NearestAncestor;
};
```

`Scene::TryGetInheritedComponent<MySettings>(entity)` then returns the local value or the
nearest ancestor value. Normal `HasComponent`, `GetComponent`, systems, and serialization stay
local. A future rigid-body hierarchy can select an explicit policy rather than silently making
all components inheritable.

## Rendering behavior

Standalone meshes use `Mesh::EntireAsset`. Instantiated glTF mesh nodes use their source node
index. The private renderer loads that node, detaches it from gltfio's hidden transform graph,
and drives it with the ECS world transform. This lets model children be selected, transformed,
hidden, and extended independently while preserving a single source asset identity.

Source indices are resolved explicitly: gltfio's entity list is partitioned by
renderability and is not the glTF `nodes` array. Renderer-private node names
provide stable source-index lookup without changing source files or ECS names.
Children of one model root share a loaded glTF and its textures and GPU buffers;
separate model roots retain independent transforms and material overrides.

Imported PBR materials and textures are retained unless a material override is
assigned. `Mesh::MaterialAsset` overrides all primitives on that entity;
`Mesh::Materials` contains per-primitive slot overrides. Zero preserves the
original material. The inspector exposes typed asset pickers for these slots.
New model instances discover slots from the source metadata, while existing
scenes discover missing slots when the renderer loads their meshes.

Editor ray selection tests triangles, not oversized model bounds. Static glTF
triangle lists, strips, and fans with standard position/index accessors use
shared CPU geometry and a BVH. Hover outlines are light blue, selected outlines
blue; selecting a model root outlines its renderable descendants. These helpers
remain Scene-view-only and respect entity visibility/locking. Framing and
marquee selection use transformed mesh bounds rather than node origins.

CPU editor geometry currently does not decode Draco/meshopt-only payloads,
sparse accessors, or animated skin/morph deformation. Filament rendering still
supports its own glTF capabilities; exports with ordinary, non-sparse static
geometry provide exact editor triangle picking and outlines.

## Native crash diagnostics

GUI editor sessions capture Python fault traces and native stdout/stderr in
`BAZZALT/data/Logs/editor-runtime.log` under the platform application-data
directory. The previous session is retained as `editor-runtime.previous.log`.
This includes native abort output that cannot be caught by Python exceptions.

Model bounds queries before renderer initialization or after shutdown return
empty results. Detailed-model outline strokes are batched by color/depth policy,
not emitted as individual renderables. Interactive rendering uses larger
Filament command, render-pass, and driver-handle arenas than headless tests.

To exercise native presentation rather than only CPU/NOOP behavior:

```powershell
python -m Editor.tests.native_model_viewport_smoke path/to/model.glb vulkan --editor
```

This manual GPU test uses a temporary project and a hidden Qt-native window,
exercises model drops, root/mesh selection, inspector construction and ticks,
and does not save user preferences or modify the original model. Omit
`--editor` to test the surface directly, including resize and teardown.
It requires a real graphics backend; it is not an offscreen CI test.
