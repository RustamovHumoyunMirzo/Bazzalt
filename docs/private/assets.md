# Asset database and import pipeline

## Identity and `.meta`

Each source below the project asset directory receives adjacent YAML metadata.
The UUID is the persistent reference used by components and scenes.

```yaml
FormatVersion: 1
UUID: "d83ccda4-9d20-46b5-91d8-198a445c59ef"
Importer: "Bazzalt.glTF"
ImporterVersion: 1
SourceHash: "07dfd64c6cc7e164"
CachePath: ".bazzalt/Cache/d83ccda4-9d20-46b5-91d8-198a445c59ef/07dfd64c6cc7e164.gltf"
Settings:
```

Commit sources and `.meta` files. Do not commit `.bazzalt/Cache`; it is derived
and disposable. Move a source together with its metadata to preserve identity.
Deleting metadata generates a new UUID. UUID zero is never assigned to assets.

## Import lifecycle

On project open, the private database:

1. Reads or creates metadata.
2. Selects the most specific importer.
3. Computes a source fingerprint.
4. Reuses a matching existing cache artifact.
5. Otherwise imports to `.bazzalt/Cache/<UUID>/` and updates YAML.
6. Indexes the result by UUID and normalized source path.

Changing the source hash, importer version, or removing the artifact triggers
reimport. Old `Bazzalt.Raw` metadata upgrades when a specialized importer is
introduced.

## Supported formats

| Source | Importer | Cache/runtime behavior |
|---|---|---|
| `.gltf`, `.glb` | `Bazzalt.glTF` | Loaded with gltfio |
| `.png`, `.jpg`, `.jpeg` | `Bazzalt.Texture` | Filament stb decoder |
| `.mat`, `.shad` | `Bazzalt.FilamentMaterial` | Source copied; editor compiles only used shaders |
| `.matinst` | `Bazzalt.FilamentMaterial` | Versioned JSON material; references shader and image UUIDs |
| `.filamat` | `Bazzalt.FilamentMaterial` | Compiled package copied |
| `.obj`, `.fbx` | `Bazzalt.Filamesh` | `filamesh` compiler |
| `.filamesh` | `Bazzalt.Filamesh` | Runtime mesh copied |
| other | `Bazzalt.Raw` | Byte-for-byte copy |

Relative JSON-glTF resource URIs are validated, copied beside the cached model,
and included in its aggregate fingerprint. External buffer/image changes thus
invalidate the model. Absolute paths and parent-directory escapes are rejected.
GLB embeds resources in one file.

The authoring tools operate only on trusted editor inputs and are never exposed
through the game API.

## Runtime queries

```cpp
#include <Bazzalt/AssetManager.h>

auto byId = Bazzalt::AssetManager::GetAsset(assetUuid);
auto byPath = Bazzalt::AssetManager::GetAsset("Assets/Models/Robot.glb");

if (byId && byId->State == Bazzalt::AssetState::Ready) {
    Bazzalt::UUID stableId = byId->Id;
}
```

Store `AssetInfo::Id`, not paths. Cache paths change after reimport. The manager
is read-only and returns `std::nullopt` without a bound host. Scanning, imports,
metadata edits, and cache cleanup remain private.

## States

- `Unknown` — unresolved state.
- `Ready` — valid cache artifact available.
- `NeedsImport` — stale metadata/artifact.
- `Missing` — reserved missing source/reference state.
- `Failed` — import failed.

Currently, project opening fails when any scanned asset cannot be imported.
