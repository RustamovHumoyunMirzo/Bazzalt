# Editor resources and localization

The editor owns one `ResourceManager` and one `LocalizationManager`. They are
created before the main window and exposed as `Editor.Resources` and
`Editor.Localization`, so widgets do not need to resolve paths or parse locale
files independently.

## Resources

Editor-only files live below `Editor/assets`. Resolve them with
`ResourceManager.Resolve()`, `Icon()`, `Pixmap()`, or `ReadText()`. Resolution
rejects paths that escape the asset root. New editor icons should normally be
placed under `Editor/assets/icons`; the existing docking lock and check icons
remain compiled Qt resources so existing panel visuals and paths stay stable.

```python
icon = editor.Resources.Icon("icons/import.svg")
text = editor.Resources.ReadText("templates/default.txt")
```

## Rendering preferences and restart-required settings

Edit > Preferences > Rendering offers Automatic and the backend names reported
by the native runtime build. Automatic uses Filament's platform default. A GPU
and compatible driver are still required: inclusion in the build is not a
hardware availability guarantee. The supported SDK flags are
`BAZZALT_FILAMENT_OPENGL`, `BAZZALT_FILAMENT_VULKAN`,
`BAZZALT_FILAMENT_METAL`, and `BAZZALT_FILAMENT_WEBGPU` in CMake; custom SDK
builds must configure these flags to match their actual compiled drivers.
WebGPU is off by default; Metal is only exposed on Apple builds. No-op/testing
drivers are intentionally not offered as viewport renderers.
See [Filament's backend definitions](https://github.com/google/filament/blob/main/filament/backend/include/backend/DriverEnums.h).

The saved backend is configured before any native viewport initializes. Apply
does not recreate live swap chains. Changing it prompts Restart Now / Restart
Later with one generic warning line; restarting uses the normal unsaved-scene close
workflow and preserves the hub's project path and editor version. Native scene
locks are released before the replacement process launches. Both development
Python launches and packaged Nuitka executables are supported. Pending changes
remain saved when restarting later.

File > Project Settings edits name, company, version, and description. Unknown
metadata properties are preserved. A successful name change prompts the same
restart workflow; cancelling or a failed save does not. Identity/path fields
are read-only. The engine remains independent of editor preferences and UI.

Scene preferences include Show scene viewport orientation widget. This applies
immediately, is persisted with editor preferences, and disables both rendering
and hit testing when hidden.

Edit also provides Copy, Paste, Duplicate, Rename, Delete, Select All, and
Deselect All with standard shortcuts. Commands follow the focused text field,
Console, Asset Browser, or entity selection. Duplicate is undoable, keeps parent
and scene ownership, clones descendants, and preserves component enabled states
without replacing the existing copy clipboard. Hierarchy and Console use the
global commands in the main editor to avoid duplicate shortcut registrations.

## Localization

Locale catalogs are flat UTF-8 JSON objects in `Editor/assets/locales`. UI code
stores translation keys rather than English labels and calls `Translate()` at
display time. Placeholder values use Python named formatting.

```json
{
  "asset.import_failed": "Could not import {name}"
}
```

```python
message = editor.Localization.Translate("asset.import_failed", name=asset_name)
```

`SetLocale()` validates and loads a catalog, then emits `LocaleChanged`. Shared
menus, the editor title, empty docking state, docking context menus, tooltips,
and floating-window fallback titles subscribe to this signal. Add every new
visible editor string to the locale catalog and retranslate long-lived widgets
when the signal is emitted.
