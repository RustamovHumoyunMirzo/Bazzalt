# Shader Graph implementation

`Editor/shader_graph.py` owns the versioned JSON format, node catalog, validation,
typed DAG lowering, deterministic Bshader generation and destructive conversion.
It never evaluates source as Python. Inputs contain source node identities, not
expressions. Only the closed catalog can produce shader expressions. Keep file,
node, nesting, numeric and string budgets bounded. Dependency chains are limited
to 128 nodes; memoize longest paths as well as checking recursion depth. Check disconnected cycles as
well as the output subgraph. Emit shared expressions once per shader stage.

`Editor/gui/panels/shader_graph.py` uses Qt Graphics View. The palette supports
search and drag/drop. The canvas owns movable nodes and connections; connections
are replaced transactionally after validation. Node properties use shared editor
fields, the palette uses localization, and painting uses ThemeManager tokens.
The panel has its own bounded QUndoStack; Edit routes undo/redo to it when focused.
Autosave writes atomically, ignores unchanged documents and tracks saved revisions
to reject overwriting external edits. A directory watcher avoids Windows file
locks preventing atomic replacement. A pinned panel refuses ordinary asset switches.

Conversion writes generated `.shad` source and moves the `.meta` UUID sidecar;
the graph file is deleted only after publication. Never overwrite an existing
destination. Conversion undo checks that the generated source was not modified
externally. It restores original graph bytes and moves the UUID sidecar back.
Graph history is session-local; opening another graph clears it. The word
"locked" describes managed graph authoring, not encryption or source protection.

The native material importer recognizes `.shadg`. MaterialCompiler generates
Bshader before existing translation, reflection and matc compilation. Cooked
shader products use the same cache suffixes as source shaders. Keep extension
acceptance in editor pickers, runtime reflection, RenderAssets, Compiler build
and content filtering aligned. Generated texture-default metadata comments are
strict name/UUID pairs; parse them into reflection for both graphs and unlocked
source. The runtime consumes binary material packages/reflection and does not
depend on the Python graph module, Qt or a node evaluator. Public C++/Lua material
APIs continue using the asset UUID without a separate graph API.

## Isolated previews

`Editor/shader_graph_preview.py` owns one bounded worker, one active request and
one replaceable pending request. Snapshot asset records on the UI thread. Compile
and render on the worker, ignore stale generations, and close the renderer on its
own worker before shutdown. Never create preview entities in the user's scenes.
Compile source snapshots into private staging directories. Publish complete products
on the UI thread only if source bytes and the current asset cache identity still
match. Graph inspection defers live reflection publication until its binary is
ready. Periodic material preparation defers the actively edited graph to this
worker; Play/build preparation remains a blocking correctness barrier. Shader
products can still publish when the isolated image preview is unavailable.

`src/EditorBridge/ShaderPreview.cpp` is a private pybind surface. Its raw Filament
engine, scene, camera, light, headless swapchain and readback target are independent
of Runtime::Engine and AssetManager state. Geometry, bytecode, uniforms and image
buffers are bounded. Destroy renderables before instances/materials and destroy
attachments after targets. Readback storage remains owned until its callback;
completion uses an atomic flag. The bridge releases the GIL while rendering.

Sphere geometry includes UVs and tangent frames. OBJ/glTF/GLB geometry uses the
bounded model-preview loader; optional detail retains UVs, normalized vertex colors and inverse-transpose
node normals. Missing data receives generated fallbacks. Draco/sparse/unsupported
model encodings and scene-fed render textures require the ordinary scene/model
pipeline, not silent emulation in this preview. The preview is a sampled image;
Update Preview resamples Time shaders. It is not a second live game viewport.

## Extending and testing

Add a NodeSpec to the central catalog and enforce typed inputs, stage restrictions
and safe defaults. Add localization keys and update the public shipped-node
reference. Do not allow arbitrary code snippets in node JSON. Prefer existing
Bshader intrinsics; when adding a language feature, test both the graph and
handwritten shader paths. Maintain What's New for changes to nodes or editor UI.

Run `python -m unittest Editor.tests.test_shader_graph`: every shipped node must
compile through the real Bshader translator and matc. Also cover malformed data,
cycles, missing references, type rejection, UUID-preserving conversion and history.
Run `BAZZALT_SHADER_PREVIEW_TEST=1` for the real GPU image check on a capable GPU.
Linux GPU CI and capable Mac GPU CI run it. No GPU or dependency builds are
required to run pure graph validation; native compilation is limited to the
changed engine/bridge targets against the existing dependency SDKs.
