# Scene environments and resource previews

## Authoring

Select a scene row in Hierarchy to inspect its Environment section. Settings belong
to that scene, not the editor camera or project. Only the active scene supplies
lighting to Scene and Game views. Changes mark the scene dirty and are included
in `.bscene` saves; active-scene environment edits also participate in undo/redo.

The environment-map picker accepts HDR, EXR, and precomputed KTX1 cubemaps.
Source Type selects Environment Map or Material and shows only the matching
asset picker. Both references are retained when switching, but only the selected
type is used by the renderer and preview. Clearing the selected source removes
both indirect lighting and the skybox:
the renderer then uses Clear Color only. This never creates a directional light.
Existing scene files without an Environment section receive this empty default.

| Field | Meaning |
| --- | --- |
| Source Type | Environment Map or Material; persisted per scene |
| Environment Map | UUID of the imported environment source |
| Material | Optional `.matinst` environment configuration |
| Intensity | Nonnegative Filament intensity in lux; default 30000 |
| Rotation | XYZ degrees in the inspector, radians in C++; affects IBL |
| Clear Color | Background when no skybox is visible |
| Image-Based Lighting | Enables imported reflection and diffuse lighting |
| Skybox | Independently shows the environment background |
| Show Sun | Enables Filament's skybox sun option; does not create a light |

Filament's built-in skybox does not expose texture rotation. Rotation therefore
rotates lighting, not the sky image. The material is a configuration contract,
not an arbitrary surface shader executed as a skybox: its first environment-valued
Texture2D parameter overrides the map; `color` or `baseColor` Float3/Float4 overrides
the clear color. Other surface shader parameters have no skybox meaning.

## Importing and inspecting environments

Select an environment asset in Asset Browser to see source dimensions, IBL cache
availability, cubemap resolution, sample count, and a panoramic preview.
Apply/Reimport persists options in the source's YAML `.meta` file. UUIDs remain
stable while source contents and import settings determine cache identity.

HDR/EXR imports run Filament `cmgen` without a shell or visible helper window.
Conversion uses a private ASCII-path temporary directory because the Windows
tool cannot reliably read Unicode filenames. The importer copies sources and
verified outputs using Unicode-safe filesystem APIs and cleans up its staging
directory. If a Unicode TEMP path has no ASCII short-path alias, configure TEMP
to a writable ASCII path. Importer version 2 invalidates older conversions that
may have silently generated cmgen's fallback grid.
They produce a prefiltered reflection cubemap, skybox cubemap, diffuse spherical
harmonics, PNG inspector preview, and HDR sampler2D image. Supported cubemap
resolutions are 32, 64, 128, 256, 512, and 1024; sample counts are 64 through 1024
in powers of two. Defaults are 128 and 256 respectively. Missing companion
outputs trigger regeneration. Production packaging includes `cmgen.exe` beside
the editor under `tools/filament`; development uses the configured Filament tool.

Precomputed KTX1 cubemaps are used directly and do not expose conversion options
or a generated 2D preview. KTX2 is not supported by this environment loader.
Material Texture2D pickers accept HDR/EXR sources as well as PNG/JPEG; the importer
provides a floating-point 2D image for HDR/EXR samplers. Cubemap KTX assets are not
accepted as Texture2D values.
Material shader compilation also stages `matc` inputs and outputs in an ASCII
temporary directory, then atomically publishes the package to the Unicode-safe
project cache. This does not change source files or require renaming projects.

## Public C++ API

```cpp
#include <Bazzalt/Scene.h>
#include <Bazzalt/SceneEnvironment.h>

auto environment = scene.GetEnvironment();
environment.Mode = Bazzalt::SceneEnvironmentMode::Map;
environment.SourceAsset = environmentAssetUUID;
environment.Intensity = 12000.0f;
environment.Rotation = {0.0f, 0.5f, 0.0f}; // radians
if (!scene.SetEnvironment(environment)) {
    // Invalid nonfinite values, negative intensity, or invalid clear color.
}
```

`GetEnvironment()` returns a const reference. `SetEnvironment()` validates a copy
and returns success without exposing renderer objects. `SceneEnvironment::IsValid()`
can be used before assignment. Source and material references are persistent UUIDs;
missing assets do not produce an implicit fallback sun. Serialization stores a
versioned Environment mapping alongside the scene UUID and entities.
Environment version 2 stores Mode explicitly. Version 1 scenes migrate to
Material when they have a material reference (matching their previous precedence),
otherwise Map. Editor grid fog uses a finite cutoff so it does not obscure the
scene skybox; Game views do not receive editor fog.

IBL supplies indirect illumination, not a directional shadow map. Scene views
enable ambient occlusion for contact shading; Game views use each camera's
post-processing Ambient Occlusion setting (enabled by default). Projected shadows
require a shadow-enabled Light and shadow-casting/receiving geometry. The editor
grid is an unlit overlay, not a ground surface that receives object shadows.
Assigning an environment never creates a hidden sun.

## Shared InspectorViewport widget

`Editor.gui.widgets.InspectorViewport` is a reusable, Qt-painted image preview.
`SetImage(path_or_QImage)` loads a preview, `SetImage(None)` clears it,
`HasImage()` reports availability, `SetClearColor(QColor)` changes its background,
and `ResetView()` restores fit-to-panel framing. Wheel input zooms, dragging pans,
and double-click resets. Pass localized `empty_text` to its constructor.
`ComponentSection.AddViewport(widget)` spans the inspector's field columns.

It deliberately owns no native window, Filament engine, or swapchain. Multiple
previews can coexist without competing with the editor's rendering surfaces.
The preview surround is darker than the inspector in both themes and follows
palette changes. An explicit clear color is painted inside an 8-pixel surround.
Component forms use a 24-pixel minimum field-row height and a uniform 6-pixel
vertical gap, with top alignment so spare panel space never stretches the rows.

## Fog authoring and visibility

Scene fog edits use the same `SetSceneEnvironment` patch path as environment controls. Validate environment asset types when the asset reference or source mode changes. Retain unresolved references during unrelated atmosphere edits, including a full environment dictionary round trip.

The shared analytic defaults start at 10 metres and exclude background pixels. `Editor.gui.controller._InitialVolumetricSettings` initializes a volumetric atmosphere with density 0.002, start 0, integration limit 1000 metres and background scattering. The previous editor preset used density 0.02 and a 200-metre limit; re-selecting Volumetric migrates that exact preset to the calibrated one. Custom ranges are retained. If height and falloff still match the original values (0 and 0.05), initialization also sets falloff to zero. Apply the changes in one history transaction. Rejected patches are reported and the inspector refreshes from the scene.

The GPU capture regression changes mode through the actual inspector and measures pixel differences in both Scene and Game viewports. Keep this coverage alongside the low-level fog renderer tests, since a dense test material alone does not validate the authoring defaults.

The volumetric pass composites over a display-referred source. Normalize Henyey–Greenstein by 4π, expose directional illuminance with the source camera and apply a bounded highlight response before blending. Do not add raw lux-scaled light to the already tone-mapped source. Scattering is tinted by the fog medium. The GPU regression includes a 100,000-lux sun, verifies that fog does not clip the full image, and checks zero-opacity bypass.

Fog distance reconstruction unprojects reverse-Z surface depth through the public GL-convention camera projection (`z = 1 - 2 * depth`). The rendering projection can have an infinite far plane; the finite culling far plane must not be used to linearize it. The volume uses positive view-space Z. Perspective segment lengths are divided by the normalized ray’s forward cosine to obtain physical world distance. Orthographic distance starts at each pixel’s near-plane origin. GPU checks cover near/far attenuation and verify that uniform fog remains vertically uniform while positive height falloff increases density below the reference height.


### Blended model coverage

A post-process depth texture normally omits glTF BLEND surfaces. Treating those pixels as background integrates the full fog range through nearby foliage. The glTF provider has a private alpha-coverage depth variant. It uses Filament’s supported transparent-shadow/depth variants. Do not add a synthetic material vertex program or rewrite gl_FragDepth in normal blended materials to obtain fog coverage; those changes affect native fog and normal rendering variants too. Fog depth excludes coverage below 0.5; this approximates blended foliage and is not physically exact multilayer transmission through glass.

`FogRenderer::Prepare` runs before `beginFrame`. Targets are keyed by source view and pixel dimensions. `RenderBackend` supplies both the source camera sizes and every Game-preview destination size, so rendering never creates a framebuffer or material instance inside a pass. Unused variants are retired in the next preparation. Each target owns its depth and composite views, quad and composite scene. Never reuse mutable pass views across surfaces with different cameras or sizes.

Coverage instances are cached by original material instance and invalidated by `RenderAssets::GetMaterialBindingRevision`. glTF material creation/destruction and baked-atlas texture replacement advance that revision. Asset uploads finish before their renderables enter the scene. Copy texture bindings on creation/invalidation; update alpha factors and UV matrices in place. Do not duplicate/destroy instances every frame. Prepared bindings are applied for the depth draw and the actual original bindings (including per-view LOD overrides) are restored before color rendering. Depth-only instances remain alive through queued GPU submission.

Private diagnostics expose `fog_material_creations`, `fog_cached_materials` and `fog_target_creations`. In a steady scene without edits or resizing, creation counters must remain constant. Resizing can create new variants, but live cache sizes remain bounded. GPU regressions cover independent Scene/Game camera positions, three-surface progress, texture alpha holes, differently sized viewport resizes, and disabling fog. The manual forest stress test uses an isolated model copy, real HWND surfaces as well as captured surfaces, verifies GPU progress/resource counts during movement, and measures shutdown in a timed subprocess.


### Froxel volume implementation

The implementation follows the supplied volumetric fog guide with rasterized volume passes. Filament’s public renderer does not offer compute dispatch. Each viewport owns two RGBA16F atlases representing a 3D grid, using one cell per 16 screen pixels and 4–128 quadratic depth slices (`Steps`). The atlas has eight columns and a quality-sized row count. Runtime preparation rounds the maximum requested active quality to a 4/8/16/32/64/128 capacity bucket; changing within a bucket reuses its resources. Crossing a bucket or resizing prepares a replacement before drawing. Unused cells are cleared by the injection shader, and sampling clamps XY coordinates inside each tile. No sampler points outside the allocated atlas when a quality change arrives after preparation; that frame uses the prepared capacity and the next frame applies the new quality.

`fog_medium.mat` injects world-space density and source illumination. RGB stores source radiance per world unit and alpha stores extinction per world unit. The ambient source is always present; directional HG scattering modifies illumination only. `fog_integrate.mat` writes cumulative scattering RGB and transmittance alpha at each slice’s far boundary. It converts each view-Z segment to physical ray length and uses the exact exponential integral, with a zero-extinction limit. The compositor reads the preceding boundary and integrates the partial final slice to actual scene depth. This avoids interpolation across the depth discontinuity at a foreground object. Sky uses the configured maximum view-Z, subject to AffectSky.

StartDistance uses positive view-Z. Its automatic fade width is clamped between one and five world units (half StartDistance). Both whole and partial slices integrate the smoothstep antiderivative, so no fog leaks before the start and the first slice cannot switch abruptly at a midpoint. Start zero disables the fade. No history or stochastic jitter is used. Density noise remains in world space. Constant density, no noise, and isotropic scattering provide the baseline diagnostic profile.

These private passes still compose against the existing display-referred scene capture. Bounded, camera-exposed directional illumination preserves that pipeline’s range. This is not an HDR pre-tonemapping integration, volumetric shadow-map implementation, or multilayer glass solution. Moving composition before Filament’s internal postprocessing requires a separate backend integration that retains depth-dependent effects. Do not describe the atlas adaptation as native compute or claim those unsupported stages are present.

The atlas integration evaluates each cumulative slice independently because raster shaders cannot perform a compute prefix scan. Its cost grows quadratically with depth slices and linearly with reduced XY resolution. Profile high Steps values, especially across multiple viewports. No CPU volume evaluation, readback, per-frame texture creation, or direct graphics-driver APIs are used.


### Entity-owned local fog

`Components/LocalFog.h` contains an independently enabled, oriented box and a nested FogSettings value. Scene serialization registers it as Bazzalt.LocalFog. Lua uses the ordinary component Get/Set write-back operations. The bridge flattens FogSettings for the inspector and validates a candidate before committing edits.

Prepare allocates one cached injection view/material per local entity per destination target, before frame submission. They share the target medium attachment and use additive coefficient blending. No fixed local-volume count is imposed. Removed or disabled components retire their cached passes. Rendering computes world-to-local rays using the entity’s full inverse world matrix, clips each slice to the oriented box, samples within the clipped interval, and weights coefficients by the covered segment. Singular transforms are skipped. Mixing happens before integration, so overlap does not depend on draw order. Local maximum opacity bounds each volume’s optical depth. Start fade, density and soft box edges remain sampled at froxel resolution; thin volumes require appropriate Steps and range.

Exponential local fog uses uniform density, Height uses its local Y height law, and Volumetric also enables world-space noise and directional anisotropy. Native analytic scene fog can coexist with local fog; the compositor does not inject that scene fog twice. Local fog works when scene fog is disabled. Sky inclusion is evaluated per injection. Editor-only hidden entities skip injection in Scene while Game stays unchanged. Selected bounds use normal guide lines and never become game geometry.


### Allocation budgets

Private fog_texture_bytes measures owned scene color/depth and fog atlas texture storage. fog_peak_texture_bytes tracks its peak across preparations. These counters exclude Filament internal attachments, driver allocation overhead and deferred deletion storage, so they are not total resident VRAM. A target uses 12 bytes per presentation pixel plus 16 bytes per atlas cell. At 32 slices the two atlases use one quarter of the former fixed 128-slice allocation; total owned texture storage falls from 20 to 14 bytes per presentation pixel for dimensions divisible by sixteen (30%). Quality changes intentionally allocate only when crossing a capacity bucket. The regression must check pixel equivalence as well as the byte counters. The experimental parallel scan did not pass equivalence checks and is not part of this implementation; the validated cumulative integration remains quadratic at high slice counts.
