# Editor toolbar and 3D gizmos

The fixed `Editor.Toolbar` sits directly below the menu bar and spans the editor window. It is
not dockable or floatable. The compact toolbar uses Qt standard icons, so controls follow the
host platform and do not add another icon asset set. Their alpha masks are tinted with the
active theme's normal and disabled text colors and refresh immediately after a theme change.
The toolbar has a localized title, which is also used by Qt's native toolbar visibility menu.

## Transport controls

`EditorToolbar` exposes `PlayAction`, `PauseAction`, `StepAction`, and `StopAction`. User input
updates its local `PlayState` and emits `PlayRequested`, `PauseRequested(bool)`,
`StepRequested`, or `StopRequested`. `GetPlayState()` and `SetPlayState(state)` let the future
editor runtime synchronize authoritative state. No engine initialization, ticking, or scene
mutation is performed by this toolbar.

## Gizmo mode

The left-side mode selector offers Select, Translate, Rotate, and Scale and emits
`GizmoModeChanged(GizmoMode)`. Use `GetGizmoMode()` and `SetGizmoMode(mode)` for keyboard
shortcuts and persisted editor preferences. Its popup is an `EditorMenu`, so spacing, colors,
icons, hover behavior, and localization match the application menu system.

## Object menu and keyboard tools

Number keys `1`, `2`, `3`, and `4` activate Select, Translate, Rotate, and Scale.
The Object menu and toolbar share the same actions and current mode. Mode shortcuts
do not intercept typing in inspector/search fields, modal dialogs, active Game input,
camera navigation, or an ongoing gizmo drag.

Object > Selection offers active-scene select-all/invert, deselection, parents,
direct children, descendants, and siblings. Sibling selection stays within each
selected object's scene, including objects at the scene root. Locked objects remain
excluded from selection.

Object > Placement can place a group at the last pointer position inside the Scene
viewport, move it to the camera's view focus, center it at the world origin, align
its world rotation to the scene camera, or reset world rotation/scale. Cursor
placement raycasts actual primitive/model surfaces, ignoring the selected subtrees;
when no surface is hit, it intersects the currently selected XY/XZ/YZ grid plane.
If there is no forward intersection, nothing changes. It is not an OS cursor warp
or a persistent 3D cursor. Group placement preserves root-to-root spacing.

Object > Snapping provides explicit 0.5-unit pivot snapping and projection of the
selection center onto the active grid plane. These commands use world transforms,
support undo/redo and per-scene dirty indicators, and process a selected parent and
child only once. Editing commands remain in Edit; camera navigation and presets
remain in View rather than appearing again inside Object.

## Geometry implementation

`Editor.gui.gizmos` contains no Qt, rendering, ECS, or engine dependency. It provides:

- `Vec3`, `Ray`, and `Plane` geometry types.
- Forward `RayPlaneIntersection` and closest ray/axis calculations.
- `PickAxis` for X/Y/Z handle hit-testing with a world-space tolerance.
- Axis and plane translation, signed axis rotation, axis scale, and view-plane uniform scale.
- `Snap` for optional translation, rotation, and scale increments.
- `GizmoDrag`, which captures an immutable drag start and returns a `GizmoDelta` from each
  subsequent pointer ray.

The Scene viewport should create its camera ray from the cursor, call `PickAxis` (or its future
render-handle picker), construct `GizmoDrag` on pointer press, and apply each calculated delta
to a preview transform. Commit that preview through the editor command/undo system on pointer
release. This separation keeps manipulation deterministic and testable before renderer and
engine integration.
