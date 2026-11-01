# Prefab framework

.bprefab v1 is bounded YAML with one source-local hierarchy. PreparedScene data parsing is shared
with scene loading; prefab import validates structure without executing component code.
PrefabAccess owns authoring and cooking. Public Prefab exposes read/instantiate/query operations.
A private PrefabLink on each instance root stores asset UUID, local-to-scene UUID mapping and
serialized baseline. Scene serialization retains expanded entities, so missing source assets do
not destroy authored work. Unknown component records survive capture and instantiation.

Remapping is declared by serialized reference key, never by scanning arbitrary strings for UUIDs.
Custom registries use RegisterEntityReference. Script property type metadata identifies entity refs.
Three-way update compares old baseline, local data and latest source at entity/component/property
levels. Root placement is deliberately instance-local. Refresh/revert validate in a staging scene,
reconstruct with stable UUIDs, and restore the previous snapshot if deserialization fails.
Native Entity handles must be reacquired after reconstruction. Serialization callbacks must be pure.

Editor authoring snapshots all loaded scenes plus changed asset/meta/attachment files for undo.
The bridge validates project asset-directory destinations and rejects authoring while playing.
C++ attachment manifests are materialized into ScriptComponents before capture; new instance
attachments are merged back for inspector editing. Play compiles prefab-only native sources and
registers a private module lookup. The cooker rewrites template and baseline script references.
Prefab script startup reuses the streamed hierarchy lifecycle and is deferred to a safe tick.
Destroying a scripted instance stops only its owned runtimes; Lua system cleanup is VM-specific.

Limits: 16 MiB, 10000 entities, single flattened source hierarchy, no variants/nested inheritance.
Run BazzaltPrefabTests and Editor.tests.test_prefabs; verify Compiler content/cooking tests and
the complete native/UI suites. Update public references, manual and what-is-new with API changes.
