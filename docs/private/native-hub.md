# Native BAZZALT Hub

The Hub uses standard PySide6 Qt Widgets with the platform's default style,
palette, fonts, focus indicators, menus, and file dialogs. It is independent of
the editor: no editor widgets, theme service, localization manager, docking code,
native engine, HTML, WebEngine, or WebChannel is imported by the Hub interface.
The shared desktop package supplies only branding, paths, settings, and versions.

## Screens

- **Projects:** searchable, sortable project table; native Add Project and New
  Project dialogs; selected-project compatible editor selector; explicit Open
  button and double-click opening. Right-click offers Open, Open With, Show
  Project Folder, and Remove From List. Removing a registration never deletes
  project files. Missing project files remain visible but cannot be launched.
- **Editors:** installed versions, installation paths, supported project formats,
  and development/installed status. Refresh rescans manifests. Locate Editors
  selects a versions root; double-click opens the installation directory.
  Downloads are not invented by this interface: installations still use the
  existing manifest-based catalog.
- **Preferences:** default projects parent directory, removal confirmation,
  remembered window size/position, and read-only storage locations. Changes use
  the existing versioned, atomic Hub settings store, not browser localStorage.

Ctrl+N opens New Project, Ctrl+O adds an existing project, Ctrl+F focuses search,
and F5 refreshes installed editors. On macOS standard Qt key sequences use the
platform's normal modifier. Dialogs are parented/modal; native controls remain
keyboard accessible. The native OS theme controls appearance; no global custom
stylesheet or forced Fusion style is installed.

## Project lifecycle

Creation requires an installed editor and writes a real `.bproject` and Assets
directory through `HubCatalog`. Failed creation leaves the dialog open. New and
added projects are selected in the library. Launch uses the chosen compatible
version, preserves Unicode paths, updates last-opened/version metadata, and
retains table selection. An explicitly requested unavailable version is never
silently replaced. The editor still owns its loading/progress window.

`Launcher/ui.py` contains native windows, pages, and dialogs. `Launcher/main.py`
owns application startup and the existing signal-based `HubBridge` controller.
`Launcher/catalog.py` retains persistence/discovery/project operations. The old
`Launcher/index.html` has been removed; Hub packaging explicitly excludes
WebEngine/WebChannel imports. Branding remains compiled into resource bytes.

## Verification

```powershell
python scripts/compile_resources.py
python -m unittest Launcher.tests.test_native_hub Editor.tests.test_compiled_resources
python -m Launcher
```

UI tests use isolated projects and an in-memory catalog, mock only external
process launch/dialog decisions, and exercise real project-file creation.
`build/hub-native-preview.png` is a test screenshot; it is not shipped.
