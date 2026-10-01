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
