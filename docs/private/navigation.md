# Navigation module

Fetch pinned sources with scripts/get_recast.ps1 or scripts/get_recast.sh before CMake configuration.

Public Navigation.h and navigation components contain no Recast/Detour types. Core collects a bounded world-space triangle snapshot and dynamically loads its sibling bazzalt_ai module through a version/size-checked dispatch table. The module links the pinned Recast, Detour, DetourCrowd and DetourTileCache libraries privately. Detour uses 64-bit polygon references on every target, including 32-bit games, to retain salt/reference capacity with layered tile meshes.

Scene destruction, Clear and Engine Stop release module-owned worlds before physics teardown. Navigation updates on the fixed simulation clock before Jolt and game fixed updates. It supports multiple surfaces per scene. Surface replacement is transactional and carries agent destinations forward. Geometry ownership and agent identity are checked using Entity generations. Baked surfaces are prepared before initial and streamed script OnCreate callbacks. No worker thread accesses EnTT or mutable Detour state.

The bake stores validated little-endian tile-layer fields and raw layer grids. It stores no pointers, native padding or Detour navmesh blobs. Loading reconstructs Detour meshes from validated layers. Obstacle carving never modifies the source bake layers. Files use version 1, a 128 MiB budget and bounded tile/agent/query capacities. Editor baking writes atomically, imports the file and sets the surface asset reference through normal scene undo. It is a synchronous modal operation.

Render sources share primitive/model CPU geometry. Jolt source mode extracts actual static collider triangles through the physics module dispatch table, avoiding Jolt headers in core or AI. Agents use a feet-based origin and bounded turning. Kinematic agents use capsule sweeps and MoveKinematic. Dynamic bodies cannot be driven by a Navigation Agent. Physical movement blocked by a collider resets crowd placement while retaining the target for retry.

TileCache supports at most eight layers touched by one obstacle. Validate the complete footprint and report an actionable error instead of carving only a subset. Moving obstacle removal/addition and tile rebuilds use bounded incremental updates. Area-mask filters are reclaimed as component masks change. Render modifiers assign areas, physics-mode sources currently use the default area. Cross-surface links, off-mesh traversal and root-motion-driven agent reconciliation remain game-level work.

When extending navigation, update the public contracts, component visitors, Lua operations, serialization/reference rules, editor controls, cooked runtime staging on every platform, THIRD_PARTY notices, source fetch workflows and native/editor regressions. Add new format versions with bounded readers. Never expose dependency pointers or allow asset-controlled paths to select a native module.
