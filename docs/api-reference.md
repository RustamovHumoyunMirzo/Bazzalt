# Public API reference

This covers headers under `include/Bazzalt`. Headers remain authoritative for
templates and exact declarations.

## `Bazzalt/Component.h`

### `Component`

Empty marker required by entity component methods and `ComponentSystem<T...>`.
It has no virtual methods or runtime state.

## `Bazzalt/UUID.h`

### `UUID`

- `UUID()` — zero/root UUID.
- `UUID(uint64_t high, uint64_t low)` — construct from halves.
- `Root()` — return zero.
- `Generate()` — random nonzero UUID.
- `TryParse(string_view, UUID&)` — parse hexadecimal UUID text.
- `ToString()` — canonical `8-4-4-4-12` text.
- `GetHigh()`, `GetLow()` — raw halves.
- `IsValid()`, `operator bool()` — nonzero test.
- `IsRoot()` — zero test.
- equality, ordering, and `std::hash` support.

## `Bazzalt/Entity.h`

### `Entity`

- `AddComponent<T>(args...)`
- `AddOrReplaceComponent<T>(args...)`
- `HasComponent<T>()`
- `GetComponent<T>()`
- `TryGetComponent<T>()`
- `RemoveComponent<T>()`
- `IsValid()` and explicit boolean conversion
- `GetId()` — transient EnTT-backed identifier
- `GetUUID()` — persistent identifier
- `GetScene()` — non-owning scene pointer
- `GetParent()`, `HasParent()`, `GetChildren()`
- `IsAncestorOf(Entity)`
- `GetWorldMatrix()`
- `SetParent(Entity)`, `AddChild(Entity)`, `RemoveParent()`
- equality compares handle and registry ownership.

Default-constructed and destroyed entity facades are invalid.

## `Bazzalt/Scene.h`

### `Scene`

- `CreateEntity(name)` — generate UUID.
- `CreateEntity(uuid, name)` — explicit UUID.
- `DestroyEntity(Entity)` — recursive destruction.
- `Clear()` — remove ordinary entities.
- `FindEntityByName(name)`
- `GetEntity(Entity::Id)` and `GetEntity(UUID)`
- `GetRootEntity()`
- `GetUUID()`, `GetEntityCount()`
- `SetParent`, `RemoveParent`, `GetParent`, `GetChildren`, `IsAncestor`
- `GetWorldMatrix(Entity)`
- `AddSystem<T>`, `HasSystem<T>`, `GetSystem<T>`, `RemoveSystem<T>`
- `GetRegistry()` — mutable/const EnTT access.

Scenes are noncopyable/nonmovable. Frame update is private to the host.

## `Bazzalt/System.h`

### `System`

- `IsEnabled()`, `SetEnabled(bool)`
- protected `OnCreate(Scene&)`
- protected pure virtual `OnUpdate(Scene&, float)`
- protected `OnDestroy(Scene&)`

### `ComponentSystem<Components...>`

Requires at least one `Component` type. Protected `GetView(registry)` returns a
mutable or const EnTT view containing all listed types.

## Built-in components

### `Components/Identity.h`

`Identity::Value` stores the entity UUID. Scene-managed.

### `Components/Name.h`

`Name::Value` is the display/search name.

### `Components/Hierarchy.h`

`Parent` and `Children` store UUID relationships. Use hierarchy methods instead
of editing them directly.

### `Components/Transform.h`

Fields: `Position`, `Rotation`, `Scale`. Methods: `GetMatrix`, `GetForward`,
`GetRight`, `GetUp`, `Translate`, `Rotate`.

### `Components/Camera.h`

Enums: `CameraProjection`, `CameraAspectMode`, `AntiAliasing`, `ToneMapping`,
`DepthOfFieldQuality`.

`CameraViewport` stores normalized bottom-left `X`, `Y`, `Width`, and `Height`.
Factory methods provide `FullScreen`, half-screen, and arbitrary `Grid` cells.

`CameraPostProcessing`: `Enabled`, `Bloom`, `AmbientOcclusion`,
`AntiAliasingMode`, `ToneMappingMode`, `Exposure`, `DepthOfField`,
`CustomEffects`.

## `Bazzalt/PostProcessing.h`

`PostProcessingStack` is an ordered, renderer-neutral collection of
`CustomPostProcessEffect` values. `AddEffect` accepts a compiled material asset
UUID. Each effect has `Name`, `Enabled`, `Order`, and typed `Parameters`.

`PostProcessParameter` factory methods support `Float`, `Float2`, `Float3`,
`Float4`, `Integer`, `Boolean`, and `Texture`. `SetParameter` replaces a named
parameter, preventing duplicate shader bindings.

`Camera`: `Projection`, `VerticalFieldOfView`, `OrthographicSize`, `NearPlane`,
`FarPlane`, `AspectRatio`, `AspectMode`, `Viewport`, `Priority`, `Active`,
`ClearColor`, `PostProcessing`.

### `Components/Light.h`

`LightType`: `Directional`, `Sun`, `Point`, `Spot`.

Fields: `Type`, `Color`, `Intensity`, `Range`, `InnerConeAngle`,
`OuterConeAngle`, `SunAngularRadius`, `SunHaloSize`, `SunHaloFalloff`,
`CastShadows`, `Enabled`.

### `Components/Mesh.h`

Fields: `MeshAsset`, `Materials`, `LayerMask`, `Visible`, `CastShadows`,
`ReceiveShadows`.

## `Bazzalt/SceneManager.h`

Static nonconstructible facade:

- `LoadScene(path)`, `LoadScene(UUID)` — enqueue transition.
- `GetActiveScene()` — non-owning host scene pointer.
- `IsLoadPending()`
- `GetLastError()`

Calls fail when no host is bound or the request is unsuitable.

## `Bazzalt/Asset.h`

### `AssetState`

`Unknown`, `Ready`, `NeedsImport`, `Missing`, `Failed`.

### `AssetInfo`

Fields: `Id`, `SourcePath`, `MetaPath`, `CachePath`, `Importer`,
`ImporterVersion`, `State`.

## `Bazzalt/AssetManager.h`

Static read-only facade:

- `GetAsset(UUID)`
- `GetAsset(path)`
- `IsAssetReady(UUID)`

Lookups return `std::optional<AssetInfo>`.

## `Bazzalt/Serialization.h`

- `PropertyMap` — ordered string/string map.
- `SerializedComponent` — type, version, properties.
- `UnresolvedComponents` — unavailable records preserved for round trips.
- `ComponentSerializationRegistry::Register<T>` — typed hooks.
- `RegisterDescriptor`, `Find`, `GetDescriptors` — dynamic registration and
  inspection.
- `SceneSerializer::GetComponents()`, `GetLastError()` — registry/error access.
  Construction and scene I/O are host-only.

## `Bazzalt/Project.h`

### `ProjectMetadata`

Fields: `ProjectUUID`, `Name`, `AssetDirectory`, `StartupScene`, `Properties`.
`CurrentFormatVersion` is the supported YAML version.

`ProjectSerializer` is host-only; game code cannot open/save projects.

## `Bazzalt/Math.h`

Defines scalar helpers, `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, and
`Quaternion`.

### Scalar API

- constants `Pi`, `Epsilon`
- `ToRadians`, `ToDegrees`, `Clamp`, scalar `Lerp`, `IsNearlyEqual`

### `Vec2`

Construct from zero, one replicated scalar, or `x/y`. Operations:
`LengthSquared`, `Length`, `Normalized`, `Normalize`, static `Dot`, static
`Lerp`, unary plus/minus, vector add/subtract, scalar multiply/divide, compound
assignments, exact equality/inequality, and left-side scalar multiplication.

### `Vec3`

Construct from zero, one replicated scalar, or `x/y/z`. It has the `Vec2`
length, normalization, dot, lerp, arithmetic, compound assignment, and equality
operations, plus static `Cross` and component-wise vector multiplication.

### `Vec4`

Construct from zero, one replicated scalar, `x/y/z/w`, or `Vec3` plus `w`.
Operations: length, normalization, `XYZ`, static `Dot`, add/subtract, scalar
multiply/divide, and left-side scalar multiplication.

### `Mat3`

Default identity or diagonal constructor; mutable/const `(row,column)` access;
`Identity`, `Transposed`, `Determinant`, `Inversed`; matrix multiplication and
matrix-vector multiplication.

### `Mat4`

Default identity or diagonal constructor; mutable/const `(row,column)` access;
mutable/const `Data`; `Identity`, `Translation`, `Scaling`, `Rotation`,
`Transform`, `Perspective`, `Orthographic`, `LookAt`, `Transposed`,
`TryInverse`, `Inversed`, `TransformPoint`, `TransformDirection`; matrix
multiplication and matrix-`Vec4` multiplication.

### `Quaternion`

Default identity and component constructors; `Identity`, `FromAxisAngle`,
`FromEuler`, `LengthSquared`, `Length`, `Normalized`, `Normalize`,
`Conjugated`, `Inversed`, `Rotate`, static `Dot`, static `Slerp`, and quaternion
multiplication.

See [Math](math.md) for conventions and examples.
