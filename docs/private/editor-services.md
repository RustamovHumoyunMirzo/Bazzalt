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
