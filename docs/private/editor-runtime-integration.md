# Editor runtime integration — alpha

The editor integrates through a private pybind11 module named `_bazzalt_runtime`. The binding
includes `src/Runtime/Engine.h`; no Python, Qt, pybind11, or editor type is visible to the engine
library. `RuntimeService` is the only Python-facing runtime service, and `EditorController`
coordinates it with widgets.

## Build

Install the pinned source dependency with `scripts/get_pybind11.ps1` or
`scripts/get_pybind11.sh`, or provide an installed pybind11 CMake package. Configure with
`-DBAZZALT_BUILD_EDITOR_BRIDGE=ON` and build `_bazzalt_runtime`. Multi-config builds place the
module under `build/Editor/<Configuration>`; the Python loader checks these development paths.

## Alpha workflow

The File menu creates, opens, and saves `.bproject` files and opens/saves `.bscene` files.
Opening a project refreshes the asset browser from project metadata. Runtime entities populate
Hierarchy recursively. Selection builds a Properties inspector with live Transform fields and
read-only component sections. Create/Delete context actions mutate the real C++ scene.

Play creates a temporary serialized snapshot, initializes the private runtime, and ticks it from
a 16 ms Qt timer. Pause suspends automatic ticks, Next Step advances exactly one update, and Stop
shuts down runtime rendering and restores the authoring snapshot. Temporary snapshots are
removed even during host destruction.

The Scene and Game panels are connected lifecycle hosts, but remain black presentation surfaces
in this alpha. Filament currently lacks a platform swapchain/presentation handle abstraction in
the private renderer. Embedding it is intentionally deferred rather than exposing Filament or
letting the engine depend on Qt. The next renderer milestone should add a private native-surface
adapter and resize/present API behind `EditorHost`.

`EditorController.ApplyGizmoTranslation` applies calculated editor gizmo deltas through the
bridge to the selected C++ entity. Rotation and scale already exist in the inspector and gizmo
math; viewport pointer routing will connect them when the presentation widget replaces the alpha
surface.
