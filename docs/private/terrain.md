# Terrain ownership and extension points

Terrain is core scene data, not an editor-owned procedural primitive. Its public
records live in `include/Bazzalt/Components/Terrain.h`; geometry/query utilities
live in `include/Bazzalt/Terrain.h`. All sample values use terrain-local metres,
with a local XZ footprint centred at the entity origin. Resolution is 2^n + 1.

## Data and mutation

`src/Terrain/Terrain.cpp` owns composition, resampling, painting, holes, spline
shaping, heightfield boolean operations and deterministic vegetation placements.
Public mutation methods validate inputs and advance a process-local revision.
Direct field/array mutation must call `MarkDirty` after validation. Revisions are
not serialized; loading and replacing a component get a fresh revision so old GPU
resources and collision caches cannot masquerade as the restored data.

The editor explicitly imports image values into sample arrays. Merely assigning
a source image never overwrites sculpting. Integer image values are linear, with
separate 8-bit/16-bit decoding; HDR sources use float decoding. Brush stamps are
cached against source modification time and reduced to 512 pixels per axis.
Do not globally change stb gamma settings to import a heightmap.

`TerrainSerialization.cpp` registers `Bazzalt.Terrain` version 1. Its bounded codec
keeps layer, prototype, placement and spline metadata in individually named YAML
properties. Sample arrays are bounded numeric scalars. Decode into a temporary,
validate everything, then publish. Keep unknown-version handling in the existing
scene serializer. `AssetDependencies.inl` recognizes nested material/model/image
UUID fields so build cooking and reverse reference tracking retain dependencies.

## Rendering, picking and physics

`TerrainRenderer` is a renderer-owned system attached with the other render
systems in Engine. It creates chunk geometry from the same composed heights used
by tools. A chunk fingerprint includes neighbouring samples for normals, coverage,
holes and material settings. An unrelated brush edit should not upload every
chunk. Choose the most detailed LOD required by active runtime/editor cameras;
Filament performs per-view frustum culling. Skirts cover adjacent LOD boundaries.
Chunks containing holes remain full resolution.
Vegetation retains exact model/material/pose keys, preserving unchanged prefixes
and matching surviving instances after removals. Height edits do not recreate
foliage. Model cache-path changes invalidate only the affected model instances.

`RenderAssets::CreateGeometry` reuses the existing material override, selection,
debug shading, visibility and resource lifetime paths. Terrain adds a separate
vertex-color stream; ordinary primitive vertex stride stays unchanged. Immutable
prototype identity shares buffers. The built-in `terrain_lit.mat` renders smooth
coverage colors and metre grid lines. Custom surface programs use disjoint
per-pixel stochastic coverage intervals, interpolated from normalized splat
weights. Each layer keeps its material, UV tiling, shading model and render state.
Intervals partition [0,1], so opaque layers fill each pixel exactly once. TAA
resolves the sampled distribution. Scene views enable TAA when layered terrain
is present. Game views retain their camera anti-aliasing settings. Coverage grain is visible with anti-aliasing
disabled. Both UV-flip settings are handled during shader lowering.

Editor/terrain_shader.py lowers imported surface shaders and embedded variants.
CMake invokes scripts/generate_terrain_material.py for built-in materials.
Import compilation emits .terrain.filamat beside the regular package. Graph
preview publication and Compiler/content.py retain that binary. The renderer
selects it for layered terrain. UV1 carries private coverage intervals. Ordinary
primitives retain their existing vertex stride. Lowering changes require a
material cache-version bump and matching built-in variants.

Editor ray picking uses authoritative full-resolution heightfield traversal, not
LOD triangles or skirts. `GetEditorMeshes` still exposes terrain chunk bounds to
framing and marquee selection. Jolt's existing Mesh Collider source collector
accepts Terrain and builds full-resolution triangles without skirts. Terrain is a
static surface unless the user deliberately configures another supported body;
generated foliage does not implicitly create physics bodies or hierarchy nodes.

## Authoring and scripting

`TerrainBridge.h` owns inspection/commands/image decoding. `Editor/gui/terrain.py`
owns input interception, brush guides and the Scene-local overlay. Each stroke
captures one ordinary scene undo snapshot and commits on release, focus loss,
tool change or shutdown. Play mutations are rejected natively as well as in Qt.
Locked entities reject terrain editing. No extra native top-level render window
is created for the overlay. Its native child promotion occurs only when enabled
over an attached native viewport; offscreen tests retain ordinary Qt children.

`terrain_fields.py` uses shared `ListInput` and filtered project asset pickers.
`collections.py` also supplies a tab container. Script `TAB` and the `Tab` rule
group properties without changing serialized property identifiers. `FloatList`
and `IntList` are bounded numeric property lists in both C++ and Lua; their
comma-separated property wire format also handles empty lists. Keep codegen,
Lua declarations, runtime setters and the inspector together when adding types.

LuaTerrain exposes public records and algorithms, with component copy/write-back
semantics matching other components. Scene samples and placements belong to the
scene; renderer handles and brush widgets never enter Lua or scene YAML.

## Checks and boundaries

Run `BazzaltTerrainTests`, the existing rendering/LOD/physics tests, and
`python -m unittest Editor.tests.test_terrain`. Run the editor shell/inspector
tests after rebuilding the bridge. Refresh API references and public navigation
with the normal documentation scripts.

Heightfield cells intersecting enabled TerrainVolume ellipsoids use marching
tetrahedra. Density starts from composed ground height minus Y. Add uses maximum
and Subtract uses minimum with the negated ellipsoid density. A fixed global
vertical grid and identical tetrahedra keep adjacent chunk extraction consistent.
BuildTerrainChunk, scene picking and physics consume this geometry. Volume edits
must preserve a valid vertical range. Sculpt/import operations validate a copy
before publishing changes. Current extraction work is bounded to four million
vertical cells per terrain.

Spline GenerateSurface creates material-backed ribbons. SurfaceCollision controls
physics inclusion. Optional serialized fields retain defaults when loading older
version-one scenes. Spline material references participate in asset dependency
tracking. MatchEdge validates adjacency and computes composed heights in the
neighbour transform, then publishes the edited terrain atomically. It upsamples
the destination when the source has a higher resolution.

Sample dimensions now reach 2049 and layer count reaches 32. The combined dense
height/weight capacity must fit 64 million float values. Keep these allocations
bounded when adding new editing methods or codec fields.

Source-authored custom surface programs blend through stochastic coverage.
Binary-only packages require a matching terrain coverage variant.
