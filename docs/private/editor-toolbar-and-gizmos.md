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

## Geometry API

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
