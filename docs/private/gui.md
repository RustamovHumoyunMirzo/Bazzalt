# Runtime GUI architecture

Public contract: `include/Bazzalt/GUI.h`, forwarding component headers in
`Components/GUI`, and `CameraRenderTarget.h`. Gameplay never initializes GUI or
gets Filament/SDL objects. C++ and Lua must remain in parity (`LuaGUI.cpp`).
Public usage/reference lives in `docs/public/0.5.0/native/gui/en.html` and
`lua/gui/en.html`; these files are the maintained source, not generated snapshots.

## Binary/module boundary

`BazzaltGui` is a separate shared target: `bazzalt_gui.dll` on Windows,
`libbazzalt_gui.so` on Linux, and `libbazzalt_gui.dylib` on macOS. Deploy it
beside the core library, including in the ScriptSDK runtime directory. Core
retains public GUI entry points and the scene-owned system/state shell; layout,
input behavior, serialization descriptors, GPU batching, font atlas and embedded
GUI material live in the module. Lua/C++ scripts still link only the core API.

`GuiModuleLoader.cpp` resolves the module from the **loaded core library's
directory**, never from project assets or the working directory. The private
`BazzaltGetGuiModuleApi` table validates ABI version and table size. The module
imports core; core does not import GUI, avoiding circular import-library builds.
Ship matching binaries/compiler/CRT; this is not a third-party plugin ABI. Keep
the module resident until process exit because component descriptors retain its
callbacks. RenderBackend destroys GUI GPU resources before its Filament engine.
Missing/incompatible modules produce an explicit runtime error, not silent GUI
fallbacks. Release manifests declare `gui_abi: 1`; Hub validates new packages but
still accepts older editor releases that do not declare this module.

In hierarchy, **Add New > GUI** creates Frames and compositional widgets. Widgets
automatically get a Viewport Frame if the parent has no Frame ancestor. Frame
roots never carry Rectangle/Text/Button components themselves. A Camera-Bound
Frame requires selecting its camera in the inspector.

## Ownership and update order

Every Scene owns a private GuiSystem installed during construction. This system
resolves layout and processes input before gameplay updates. Engine supplies
presentation dimensions after InputAccess.BeginFrame. GUI state is scene-local;
no static entity/callback pointers survive scene swaps. Script code polls pulses
instead of registering callbacks whose DLL/Lua lifetime could escape the module.

Frame is a rendering root, not a widget. Traversal starts at its children, skips
other Frame roots, honors hidden/disabled RectTransforms, and stops at 128 levels.
Children without RectTransform act as transparent hierarchy containers. Bounds
are resolved top-left, with reference scaling and ancestor clipping. Layout gets
refreshed again before presentation so runtime changes affect the next draw.

Spatial layout remains in reference pixels and is mapped through the Frame world
matrix using PixelsPerUnit. Only the Frame world Transform participates; widget
positions come from RectTransform. Picking inverse-transforms a scene camera ray
to that plane. Viewport picking uses presentation pixels; camera viewport Y is
converted from bottom-left to top-left. Spatial rays outside a camera's viewport
must be ignored, not propagate an exception into the engine tick.

## Rendering

GuiRenderer is private and owned by RenderBackend. Its embedded material is built
by matc from `runtime/resources/shaders/gui.mat`; no runtime shader compilation.
It batches adjacent commands with the same texture, preserves paint order with
global blend ordering, CPU-clips quads/UVs, and retains GPU buffer capacity.
Texture uploads use owned asynchronous BufferDescriptors, never stack buffers.
All geometry, material samplers, camera projections and destination views are
prepared before Renderer.beginFrame; drawing does not mutate those resources.
Each screen/camera destination owns its own Scene/View/camera/batches. Sharing
mutable samplers or view state between target and presentation passes can sample
stale atlas data or the wrong attachment. Preserve existing content with both
clear=false and discard=false. Custom targets do not obey swap-chain clear
options: RenderCameraView supplies a preinitialized black sky fallback when no
environment sky exists, without adding environment lighting to the scene.
Screen GUI has a separate translucent orthographic View/Scene without camera
post-processing, exposure normalized to one, and explicit layer 0x40 visibility.
The GUI material disables Filament's default UV flip to match top-left atlas
coordinates. Spatial geometry lives in the normal scene on layer 0x40.
GUI must never use editor layer 0x80 or Qt/editor widgets.

The built-in atlas is generated from small in-source glyph bitmaps. It is a
foundation fallback, not Unicode typography. Unsupported glyphs render a box;
UTF-8 values are retained. Add font assets/shaping behind this contract later.
The first texture slot is white for rectangle fills/borders. Images borrow
textures from RenderAssets; camera feeds borrow the camera's color attachment.
GuiRenderer must be cleared before those owners retire textures, before scene
switches, and before RenderAssets/Filament destruction. Renderables die before
material instances, instances before materials, and views before scenes/cameras.

## Camera targets and split screens

Writable resources are independent `.btexture` YAML assets or transient
`Texture.Create` descriptors. TextureLibrary is CPU state/validation; RenderAssets
owns their color/history/depth/target allocations. CameraSystem borrows those
attachments and owns only its camera/view. The old CameraRenderTarget path still
owns transient camera attachments for backward compatibility; explicit Texture
destinations take precedence. Asset UUIDs pass through the same LoadTexture path
used by GUI/materials. The callback resolver returns planned producer attachments
for acyclic capture edges and history for cycles; mip generation is queued after
each producer pass before downstream sampling. No CPU readback occurs per frame.

Sampler filter/wrap, HDR color formats, mip allocation and resolved MSAA settings
come from TextureDescriptor. ClearSurface initializes both images and resets them
on producer loss. Conflicting active writers are suppressed, never last-writer
races. Consumers detach before allocation retirement; camera views are detached
before borrowed targets change. Allocation failure guards release partial
resources. Camera synchronization after scripts handles live destination edits.

Editor authoring is Asset Browser > Create > Texture, asset-only Properties,
Camera Render Target picker, and ordinary Image Texture picker. The private
writer validates project containment and atomically replaces YAML using Unicode
paths; it reimports that source only, not the entire database. Public setters are
runtime overrides and never edit files. Preserve the optional Camera serialized
RenderTarget key to load pre-feature scenes. Rebuild scripts against the new SDK
because Camera's public component layout gained a UUID field.

The public `RenderTexture` handle identifies a producer by entity UUID and resolves
against an explicit Scene. GUI setters and Material/MaterialBuilder bindings have
Lua parity. Bindings live in MaterialLibrary's definition, outside MaterialValue,
so the existing script service ABI is unchanged. They are transient overrides;
hot reload retains compatible bindings, and SetTexture/reset clears them.

RenderBackend now owns a dimension-only capture SwapChain and renders all active
offscreen cameras once before viewport consumers, even when no Game panel exists.
GUI capture resources are prepared before beginFrame. CaptureGraph uses Tarjan
SCCs in producer-first order. An acyclic consumer sees this tick's completed
producer; all members of a cycle see the previous completed SCC outputs and
publish atomically. Every camera owns two color/target pairs and shared depth;
the next write attachment is never the sampled attachment. New/retired histories
resolve to no GUI image / black material. All destination samplers are prepared
before one capture begin/endFrame, using planned producer-write attachments for
acyclic edges and old read attachments for SCC-internal edges. A skipped frame
does not publish any output; no flush/wait or CPU pixel copies occur in production.
Shared world materials/spatial GUI freeze all captures as a conservative SCC;
camera-bound GUI edges retain precise dependency ordering. GUI edges are
assigned by Frame destinations. This follows Filament's render-target ownership
model: https://github.com/google/filament/blob/main/samples/rendertarget.cpp

RenderAssets borrows camera color attachments through a private UUID table.
CameraSystem detaches material samplers and clears GUI batches before resizing,
removing or destroying those attachments. Missing material feeds sample a 1x1
opaque-black fallback; asset-less ordinary samplers use white. Fallback textures
outlive material instances. Built-in Unlit/UnlitTransparent expose baseColorTexture
with UV0; custom samplers work without native GPU handles crossing public headers.

Verification: BazzaltGuiTests covers target lifecycle and material binding copies,
resets and resizing. BAZZALT_GUI_GPU_TEST=1 additionally reads actual captured
camera GUI through an unlit primitive material with no Game panel present.

CameraRenderTarget creates sampleable RGBA8 color and DEPTH32F attachments, with
dimensions clamped to 1–4096. CameraSystem owns the target and recreates it only
when changed. RenderBackend registers UUID-to-View mappings privately. Resizing
or destroying a target first clears GUI batches referencing its texture.
Camera-bound frames composite to their camera's Game viewport or offscreen
target after the world pass. Spatial GUI and camera-feed material renderables
participate in captures safely via current producer output or frozen cycle history.
Temporal recursion is bounded to one render per camera per tick; it is not a
stack of arbitrarily deep nested renders. Double buffering increases color memory.
Offscreen frame controls are display-only until texture-space input forwarding
is available; do not let their logical rectangles capture Game-surface clicks.
Before presenting a differently sized Game surface, synchronize camera viewport
dimensions. Viewport frames also work without a 3D camera; the editor uses
HasGameOutput rather than pretending a camera exists to show that surface.

## Input

SDL3's event subsystem remains input-only. InputAccess collects pointer/key
edges and committed UTF-8 from SDL text events. Qt hosts forward committed text
(including InputMethod commits) through private GameText; pending text is bounded
and published during BeginFrame. Qt pointer coordinates are converted to physical
presentation pixels. SDL-owned native-window hosts must start/stop SDL text input
when appropriate; text events are not enabled by default. Preedit/composition
visuals and virtual keyboards are not implemented here.

Input capture is UUID-based; release outside cancels click. One scene has one
keyboard focus and one input surface at a time. Loss of input focus clears all
state. Tab order is deterministic; UTF-8 Backspace removes one code point, not a
grapheme cluster. No clipboard/selection/caret/scrolling/accessibility yet. Do not
silently synthesize Latin text from physical scancodes or consume global gameplay
input. Game code can gate its actions using GUI state.

## Persistence, editor, and regression work

GuiSerialization registers stable version-1 component descriptors with finite
numeric/type/range validation. Interaction pulses/focus/capture are transient.
EcsConfig assigns stable component hashes across compiler/module boundaries.
GuiBridge uses those descriptors for editor Inspector values and validated edits;
do not add a second unvalidated GUI property setter path.

Run BazzaltGuiTests, LuaRuntimeTests, PublicScriptApiTests, InputTests and the
editor input/GUI tests. Headless native tests use Filament NOOP for ownership,
geometry/resource validation; they are not pixel correctness tests. Always compile
the actual gui.mat package; do not substitute a stub material. Real backend visual
tests remain necessary for color-space, texture orientation, clipping, and fonts.
Set `BAZZALT_GUI_GPU_TEST=1` when running BazzaltGuiTests to enable the real GPU
headless-swap-chain/readback test for rectangle placement, clipping, and glyphs.
It is opt-in because ordinary CI workers need not have a graphics device.
Any new public GUI field/method must update Lua bindings, serialization, public
references, and regression tests together.
