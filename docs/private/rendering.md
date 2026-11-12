# Rendering

## Boundary

Rendering is described through ECS components. Public code never receives a
Filament engine, renderer, view, entity, texture, material, buffer, or destroy
function. Private systems translate scene data into Filament state.

Editor camera/light guides and selection outlines use an embedded unlit,
transparent material so the skybox cannot overwrite them. They do not write
scene depth or cast/receive shadows. Camera/light guides retain depth testing
against scene geometry; selection outlines disable it to remain readable on
their own surfaces. These resources stay on the Scene-only editor layer and
are not shown by Game cameras.

Light guides use single-sided closed geometry and camera-relative pixel widths
to avoid back/front alpha overdraw and subpixel shimmering. Point-light range is
a world-space sphere and ignores transform scale. Spot-light guides show inner
and outer half-angle cones clipped by the spherical Range boundary, rather than
an unbounded cone. Directional lights have no finite range. Only selected lights
show guides; black light colors do not make their editing guides invisible.

The light inspector shows only type-applicable fields and updates them when Type
changes. Its cone angles and Sun Angular Radius are degrees; public C++ angles
remain radians. Sun radius/halo edits are applied to existing renderer lights,
not only on creation. Halo parameters describe Filament's sun disk, not the size
or brightness of an imported IBL image. Range controls the point/spot influence
cutoff, not emitter size or intensity; directional/sun lights ignore Range.

## Camera

```cpp
#include <Bazzalt/Components/Camera.h>

auto cameraEntity = scene.CreateEntity("Main Camera");
auto& camera = cameraEntity.AddComponent<Bazzalt::Camera>();
camera.Projection = Bazzalt::CameraProjection::Perspective;
camera.VerticalFieldOfView = Bazzalt::ToRadians(60.0f);
camera.NearPlane = 0.1f;
camera.FarPlane = 2000.0f;
camera.Priority = 10;
camera.PostProcessing.Bloom = true;
camera.PostProcessing.AntiAliasingMode = Bazzalt::AntiAliasing::MSAA4xFXAA;
```

Every active camera with a non-empty viewport is submitted. Lower priorities
render first and UUID provides deterministic ordering when priorities match.

AA decoding lives in `Rendering/AntiAliasingSettings.h`. None/FXAA/TAA retain
serialized values 0/1/2. Values 3–7 select MSAA 2/4, TAA+FXAA and MSAA+FXAA.
Every synchronization sets FXAA, temporal and multisample options, including
disabled states, so toggling modes cannot leave an older stage enabled.
Camera post-processing off suppresses all camera AA stages. Destination texture
sample allocation remains independent. Filament adapts requested samples to
backend capabilities; do not claim hardware-exact sample counts. Game camera,
split-screen and capture views share this path; editor navigation views do not.

View-level MSAA captures resolve into single-sample final targets, even when the
Texture descriptor requests MSAA. The pool retains single-sample target wrappers
alongside its multisampled targets over the same color/history textures. The
camera uses the larger of its requested samples and the texture sample request
for the internal AA stage, capped at the verified view-MSAA limit of 4x. The
current Vulkan path loses the device at 8x, including a plain single-sample output
test, despite advertised hardware support; no 8x camera preset is exposed.
Final post-process resolve targets are color-only, without the destination's
multisampled depth attachment. RegisterCameraOutput updates target pointers even
when texture pointers remain unchanged, retaining front/history state while
invalidating GUI views. This avoids an invalid multisampled resolve attachment
and handles switching AA/post-processing live without stale capture targets.
Shader-based SMAA, vendor upscalers and SSAA are not implemented by these presets.
GPU regressions render every mode into an HDR/mipmapped multisampled capture;
`BazzaltRenderingTests --gpu-dof` also verifies MSAA changes geometry edge pixels.
Cameras support perspective or orthographic projection, clear color, and
renderer-neutral post-processing settings. Current synchronization applies
projection, FXAA/TAA, bloom, ambient occlusion, and enablement. Clear color,
tone mapping, and exposure await full presentation integration.

Viewports use normalized bottom-left coordinates. Automatic aspect ratio is
derived from the camera's pixel viewport, so split screens do not stretch:

```cpp
auto& playerOne = firstEntity.AddComponent<Bazzalt::Camera>();
playerOne.Viewport = Bazzalt::CameraViewport::LeftHalf();

auto& playerTwo = secondEntity.AddComponent<Bazzalt::Camera>();
playerTwo.Viewport = Bazzalt::CameraViewport::RightHalf();

// Four-player layout, top-right cell:
playerTwo.Viewport = Bazzalt::CameraViewport::Grid(1, 1, 2, 2);
```

Set `AspectMode` to `CameraAspectMode::Fixed` to use `AspectRatio` instead.
Overlapping viewports are supported; priority controls their render order.

## Depth of field

Depth of field is a native per-camera effect. Focus distance uses world units;
aperture uses f-stops. Lower DoF aperture values increase lens blur without
brightening the image: the renderer fixes exposure's aperture at f/16 and
scales the circle of confusion by `16 / Aperture`. Shutter speed and ISO still
control exposure; `CameraPostProcessing::Exposure` acts as EV compensation.

```cpp
auto& dof = camera.PostProcessing.DepthOfField;
dof.Enabled = true;
dof.FocusDistance = 6.0f;
dof.Aperture = 2.8f;
dof.Quality = Bazzalt::DepthOfFieldQuality::High;
```

Low, medium, and high quality use progressively larger gather kernels.
`NativeResolution` trades performance for maximum sharpness.

The current legacy Gaussian Blur fallback shares Filament's DoF stage. When
both are enabled, camera DoF takes priority: blur must not override its focus,
aperture, or circle-of-confusion limits. These are not yet independent stacked
passes. A manual Vulkan pixel regression can be run with
`BazzaltRenderingTests --gpu-dof`; it checks live on/off rendering and the
enabled-blur conflict, rather than only inspecting native option values.

## Custom post-processing

Custom effects are authored as Filament post-process-domain `.mat` shaders,
compiled on demand by the editor and referenced by UUID. Public
code never handles a Filament material or render target.

```cpp
#include <Bazzalt/PostProcessing.h>

auto& effect = camera.PostProcessing.CustomEffects.AddEffect(
    colorCurveMaterialUuid, "Color curve");
effect.Order = 100;
effect.SetParameter(Bazzalt::PostProcessParameter::Float("strength", 0.8f));
effect.SetParameter(Bazzalt::PostProcessParameter::Float3(
    "tint", {1.0f, 0.95f, 0.9f}));
effect.SetParameter(Bazzalt::PostProcessParameter::Texture(
    "lookupTable", lutTextureUuid));
```

Effects execute in ascending `Order`; insertion order is retained for ties.
Disabled effects and zero shader UUIDs are omitted. Effect definitions and all
typed parameters are scene-serialized. The private rendering backend owns the
compiled shader resources and compositor integration, allowing the execution
implementation to evolve without changing scene data or user code.

### Engine-provided shader effects

Gaussian blur and vignette are regular ECS components placed on a camera
entity. Their Filament sources live under `runtime/resources/shaders`. CMake
compiles them with the pinned `matc`, converts each `.filamat` package to a
generated byte array, and links those bytes into `Bazzalt.lib`. Deployed games
therefore do not need loose shader files or runtime shader compilation.

```cpp
#include <Bazzalt/Components/GaussianBlur.h>
#include <Bazzalt/Components/Vignette.h>

auto& blur = cameraEntity.AddComponent<Bazzalt::GaussianBlur>();
blur.Size = 2.0f;

auto& vignette = cameraEntity.AddComponent<Bazzalt::Vignette>();
vignette.Color = {0.0f, 0.0f, 0.0f, 1.0f};
vignette.Intensity = 0.45f;
vignette.Smoothness = 0.3f;
vignette.Roundness = 0.9f;
```

The blur shader uses a normalized nine-tap Gaussian kernel. Its texel size is
derived from each camera viewport, including split-screen views. Engine effects
run before user effects; blur precedes vignette. Both components are versioned
and scene-serialized.

The entity transform defines position/orientation. Negative Z is forward and
positive Y is up.

## Lights

```cpp
#include <Bazzalt/Components/Light.h>

auto sunEntity = scene.CreateEntity("Sun");
auto& sun = sunEntity.AddComponent<Bazzalt::Light>();
sun.Type = Bazzalt::LightType::Sun;
sun.Color = {1.0f, 0.96f, 0.9f};
sun.Intensity = 100000.0f;
```

Types are `Directional`, `Sun`, `Point`, and `Spot`. Intensity is lux for
directional/sun and lumens for point/spot. Point/spot use `Range`; spot also
uses inner/outer cone radians. Transform supplies position and negative-Z
direction.

## Meshes and materials

```cpp
#include <Bazzalt/Components/Mesh.h>

auto model = scene.CreateEntity("Robot");
auto& mesh = model.AddComponent<Bazzalt::Mesh>();
mesh.MeshAsset = robotModelUuid;
mesh.Materials = { bodyMaterialUuid, glassMaterialUuid };
mesh.LayerMask = 0xff;
mesh.Visible = true;
mesh.CastShadows = true;
mesh.ReceiveShadows = true;
```

`MeshAsset` accepts glTF/GLB or filamesh-family asset UUIDs. `MaterialAsset`
accepts a `.matinst` UUID, default None, and overrides this entity's primitives.
`Materials` supplies individual primitive slots when MaterialAsset is empty.
Overrides apply to both glTF and filamesh. With no override, glTF keeps its
authored PBR materials/textures. Overrides do not inherit between model nodes.
See [Materials and shaders](materials-and-shaders.md) for the Inspector and C++ API.

Changing the mesh/material UUIDs recreates the private render instance.
Transform, visibility, layers, and shadows synchronize continuously.

## glTF

gltfio parses GLTF/GLB, creates Filament renderables, loads external/embedded
resources, and creates glTF material variants. PNG/JPEG payloads use Filament's
stb_image-backed provider. Parse data is released after upload. Each ECS model
owns an asset instance that is removed from the render scene before destruction.

## Filamesh and shaders

OBJ/FBX authoring files are converted by `filamesh` with interleaving and
compression. filameshio loads the resulting renderable, vertex buffer, and
index buffer.

Filament `.mat` sources compile with `matc` for desktop and all graphics APIs.
Runtime builds private `filament::Material` objects from `.filamat` and creates
material instances for renderables.

## Current scope

This foundation covers resource ownership and ECS synchronization. Window and
swap-chain creation, editor viewport presentation, game presentation, material
instance authoring, environment lighting, and a complete frame graph are later
layers. Public components remain backend-neutral for that evolution.


## Surface scheduling and frame completion

One Filament renderer owns the engine's monotonically increasing submission sequence across swapchains. Separate renderer frame counters and unbounded skipped-frame attempts exposed premature shared Vulkan pipeline retirement (VUID-vkDestroyPipeline-pipeline-00765) under live model/fog editing. Each swapchain submission uses a fresh presentation timestamp. Surface order rotates deterministically so the first window cannot monopolize the shared scheduler.

Each viewport keeps at most one outstanding completion fence. Fence polling is nonblocking and flushes pending command creation; a busy surface is skipped before calling beginFrame again. A skipped beginFrame also gets a completion fence covering preceding work, preventing repeated failed attempts from advancing backend cache ages without GPU progress. Private viewport_<id>_frames counts submitted frames, and viewport_<id>_gpu_frame records genuinely completed presentation sequences. Filament timing history may be unavailable across swapchain changes; report unknown timings instead of estimating GPU time from CPU latency.

Every successful `beginFrame` is immediately protected by `SubmittedFrame`. Normal completion explicitly ends it; exception unwinding also attempts to end it before leaving the surface submission. Restoring only camera/target state does not close a frame. Leaving a frame open can stop further presentation and make swapchain destruction or shutdown wait on work that was never submitted.

Resource creation belongs before frame submission. Fog prepares destination-size variants in advance and keeps its per-target views independent. See [environments.md](environments.md) for the coverage cache and invalidation contract. Regression checks must include actual Windows HWND rendering and a sustained multi-surface resize/shutdown run; a few frames in one tiny offscreen view do not establish runtime stability.


## GPU failure and editor teardown

A timed GPU fence failure is terminal for the current renderer. WaitForIdle uses a two-second deadline, records failure once, and rejects further rendering and viewport allocation. Viewport detach retains its resource graph when the initial wait fails. Never free view/material state while queued driver work may still reference it. GPU timing queries are skipped after a recorded failure.

The editor host probes GPU retirement before native shutdown callbacks. On failure it quarantines its entire Engine and loaded scene graphs until process exit, releasing scene file locks but preserving object/callback lifetimes and play snapshots. This deliberately retains memory on a terminal device-fault path. Healthy shutdown continues to release everything normally. This is not in-process device recovery and it cannot interrupt a call already blocked inside a graphics driver. CPU authoring saves and normal close confirmation remain the editor’s responsibility.

The forest flight reproduction must test native Height fog as well as the volumetric pass. The original short volume-only tests did not cover the glTF/native-fog shader variant that failed on the developer GPU. Use forward motion, large yaw/pitch changes, close-up foliage, normal viewport sizes and multiple surfaces; inspect GPU completion and Windows device-fault events, not only CPU frame submissions.


## Live fog lifecycle regression

Run `python -m Editor.tests.native_fog_lifecycle_smoke model.glb --native --seconds 60` on a real driver. The test uses a temporary project, three hidden native surfaces, camera movement, and repeated component addition/removal, enable/disable and quality changes. It checks actual presentation/completion progress, allocation stability between edits and shutdown duration. Enable the Khronos Vulkan validation layer using VK_LAYER_PATH and VK_INSTANCE_LAYERS. The supervising process fails on validation errors, native crashes and timeouts. The layer is a development diagnostic, not a shipped runtime dependency.

Fog target preparation includes offscreen cameras only when they actually render to a texture. Window camera sizes come from their presentation destinations. Preparing every active camera creates unused targets whose dimensions oscillate with differently sized Game windows. The original reproduction allocated 646 bundles in thirty seconds; the corrected schedule prepared three and reused them. Resource changes must be measured with allocation counters while rendering, not only tested on an empty scene.
