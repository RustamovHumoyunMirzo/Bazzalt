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
