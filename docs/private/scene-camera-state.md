# Scene camera persistence

The editor owns camera persistence; the runtime and game scene formats do not.
`NativeRenderSurface.NavigationState` returns version 1 state: target, yaw,
pitch and orbit distance. Eye position is derived from these together, so an
orbit or fly camera resumes the same position and view direction. Restore
validates finite bounded values before modifying any state.

The editor stores up to 128 `scene_cameras` entries in its existing app-data
SettingsStore. Keys combine normalized project metadata path and stable scene
UUID, so scene renames retain state and separate projects cannot collide.
Navigation uses a 500ms debounce; switching active scene flushes the previous
entry before restoring the destination, and accepted editor close flushes the
last view before detaching surfaces. A restoration guard suppresses save loops.

Preferences > Scene > Remember Scene camera position defaults to enabled.
Disabling skips restoration and future camera saves without deleting previous
entries. Re-enabling records the current view. Corrupt or future-version camera
records are ignored. These writes never mark game scenes dirty and never change
a gameplay Camera component. Ordinary settings save failures retain the
in-memory state and do not interrupt camera navigation or shutdown.
