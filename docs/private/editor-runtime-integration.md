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

Scene and Game panels host native presentation surfaces through the private bridge.
Qt supplies the OS window handle; the engine owns the Filament swapchain, views,
cameras, resizing and presentation. Qt's `offscreen`, `minimal` and `minimalegl`
plugins do not provide native presentation handles and never attach a swapchain.
Headless UI/ECS tests still operate without GPU presentation. Windows additionally
validates HWNDs before calling Filament, rejecting synthetic and destroyed handles.

On accepted editor close, tick/navigation timers stop, viewports detach, gameplay
stops, and the native host is released even if the hidden Qt widget remains alive.
Deferred UI callbacks have QObject contexts and are canceled when their owner is
destroyed. Delayed timer starts cannot restart a released runtime. Swapchain destruction is
flushed before Detach returns so Qt can safely destroy or reparent its surface.

`EditorController.ApplyGizmoTranslation` applies calculated editor gizmo deltas through the
bridge to the selected C++ entity. Rotation and scale already exist in the inspector and gizmo
math; viewport pointer routing will connect them when the presentation widget replaces the alpha
surface.
