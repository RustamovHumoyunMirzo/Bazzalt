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

CameraRenderTarget creates sampleable RGBA8 color and DEPTH32F attachments, with
dimensions clamped to 1–4096. CameraSystem owns the target and recreates it only
when changed. RenderBackend registers UUID-to-View mappings privately. Resizing
or destroying a target first clears GUI batches referencing its texture.
Camera-bound frames composite to their camera's Game viewport or offscreen
target after the world pass. A target skips images sampling itself. Captures exclude spatial GUI to
prevent same-pass read/write feedback, while GuiImage consumes feeds in every
Frame mode. Targets render before presentation cameras. A future render graph
can support recursive captures with dependencies/double buffering.
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
