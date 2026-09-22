# Hub, editor installations, and application data

BAZZALT Hub is the user-facing application. Run it with `python -m Launcher`. The Editor is a
versioned project process launched by the Hub and requires `--project`; it never opens an empty
workspace and no longer creates, opens, or saves projects from its own menu.

`DataPaths` uses Qt's cross-platform generic application-data location and creates the shared
`BAZZALT` root. It contains `Config`, `Editors/<version>`, and `Logs`. `SettingsStore` writes
bounded, versioned JSON through `QSaveFile`, giving atomic replacement and explicit migrations.
Hub catalog schema 2 stores project paths, installed editor versions, compatibility limits, and
preferences. Editor schema 1 currently stores theme and recent-scene state.

Projects retain the C++ `FormatVersion`. Hub-created metadata also records
`Properties.engine.version`. The Hub only offers editor installations whose declared maximum
project format accepts the project. The editor repeats compatibility validation before loading,
so stale shortcuts or manually constructed commands cannot bypass it.

Development builds register the source checkout as editor `1.0.0`. Packaged Hub installers
should place immutable editor payloads below `DataPaths.Editors()/version` (or Program Files when
installed machine-wide) and register their executable, semantic version, and supported project
format in the same catalog.

On launch, a small editor loading window validates metadata and recursively scans the commanded
asset directory without following directory symlinks. It reports file-level progress, then opens
the native asset database and startup scene. The full Editor window is constructed only after
that succeeds; failures close the process with an error instead of revealing an empty editor.
