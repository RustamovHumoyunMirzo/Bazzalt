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
disabled. Sampling uses Filament getTime(), the independent one-second frame
clock, so gameplay pause and time scale preserve convergence. Both UV-flip settings are handled during shader lowering.

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

## Selection and collection presentation

`Engine::PickEditorPrimitive` raycasts terrain against the authoritative sculpted
surface. Do not replace this with an origin-plane or bounds-only hit test.
Terrain owners must also pass the geometry classification in
`Engine::UpdateEditorOverlays` so terrain chunk renderables enter the shared
selection mask. The mask unions chunks instead of outlining each chunk.

Terrain coverage variants set `doubleSided` on their material configuration.
The default terrain material does the same, preventing the heightfield from
disappearing underneath and leaving only LOD skirts. Spline surfaces use the
terrain variant too. When changing variant generation, bump the material cache
key so existing project artifacts are regenerated.

`ListInput` owns bounded values independently of widgets. Callers supply a
localized collection title, item name and Add caption. Expansion state follows
items through reordering and removal. Terrain item fields reuse ComponentSection
for alignment, grouping, clipboard behavior and theme styling. Keep collection
styles in `Editor/theme.py`, not inline stylesheets. The offscreen GUI regression
tests cover edits and expansion, while TerrainTests --gpu verifies actual
underside pixels and hover/selection outlines on a real graphics backend.

## Solid terrain exterior and overlay ownership

The terrain base is the lower VerticalRange value, clamped below each local
height to retain positive thickness after deep sculpting. BuildTerrainChunk
shares underside vertices on an LOD grid. CellShell emits only true exterior
and hole walls, not internal chunk boundaries. Raycast reuses CellShell at
full detail. Physics disables LOD skirts but retains the real exterior.
Volume density is intersected with the base plane; the extractor includes a
slab below that plane to generate bottom faces. Boundary walls clip against
the same density so caves reaching an edge stay open. LOD skirts cannot
extend below the base.

TerrainAction belongs to ViewportPanel.ControlsMenu. TerrainEditor owns one
movable native-capable Overlay, with icon tools and the settings button as
ordinary children. Do not give the settings button another native window or
move it independently. Place changes only the parent position while dragging
and resizes only when viewport constraints change. SVG glyphs live in
Editor/assets/icons/terrain and are tinted for the active theme and tool state.

## Spline editing and modal movement

TerrainSpline keeps Points and optional InTangents/OutTangents arrays. Tangents
are offsets in terrain-local coordinates. Empty or zero tangent arrays describe
linear spans. TerrainGeometry::SampleSpline is shared by ribbon generation,
heightfield shaping and the editor bridge. Optional per-point tangent fields
retain compatibility with older version-one scenes.

Editor/gui/terrain_spline.py owns point selection, live preview, handle hit
testing and the companion overlay. TerrainEditor delegates spline-mode input
to it. Point markers take priority over overlapping gizmo arrows. The bridge
converts world-space edits to local coordinates, validates the candidate spline
and publishes it atomically. The editor chooses the point target before
controller transform routing, so moving points never translates their entity.

NativeRenderSurface owns the modal G gesture and reuses GizmoDrag. Axis changes
recompute cumulative movement from the original ray and origin. The controller
captures transforms or spline data at gesture start. Cancel restores those
values and the previous dirty-scene set before dropping the pending history
command. Applying commits exactly one command. Scene focus loss cancels a G
gesture. Keyboard handling belongs to the scene surface so text fields retain
normal typing.

## Gameplay paths and atmosphere

`Bazzalt/Spline.h` owns the shared Bézier sampler. Terrain converts its authoring anchors and tangent offsets to this representation. Gameplay code can use the same math without a Terrain component. Arc caches are shared across immutable copies and replaced on edits. Entity array placement sorts application by hierarchy depth while retaining requested array positions.

Scene fog lives in `SceneEnvironment.Fog`. Serialization environment version 3 adds validated fields while reading versions 1 and 2 with defaults. `FogRenderer` privately composites depth-aware volumetric integration. Prepare its per-view targets before beginning a Filament frame. Restore caller render targets and viewports after drawing and detach external targets from the composite view before resources can be destroyed. Keep viewport-space GUI rendering after scene atmosphere.

Spline overlay visibility is independent of authoring state. Tab and the controls button toggle it. The gizmo controller compares current authoritative poses before uploading changes, so play-mode physics still moves handles without rebuilding unchanged UI every tick. G axis guides use three clipped parallel lines offset by a screen pixel at the object depth.

Material polling uses `CollectMaterialUsage` in the native bridge to walk references without building full hierarchy snapshots or expanding unrelated component and spline metadata. Play-mode inspector snapshots refresh at 10 Hz, independent of the rendering and physics cadence. Direct edits and gizmo commits still refresh immediately.

Post-processed Filament views ignore exported depth attachments. Volumetric fog therefore renders a separate unprocessed depth view when needed, then composites over the processed color. Its scene, camera and target references are detached after each draw. This extra geometry pass applies only to volumetric mode. Analytic height/distance fog stays on the native path.
