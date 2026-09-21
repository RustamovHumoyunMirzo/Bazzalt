# Editor panels

BAZZALT creates its standard panels inside `Editor.gui.application.Editor`. They share the
same `ThemeManager`, `ResourceManager`, and `LocalizationManager` as the menu and docking
system. The initial workspace places Hierarchy on the left, Scene and Output in the center,
Properties on the right, and Asset Browser with Console below. **Reset Workspace** restores
this arrangement.

The panel instances are available as `Editor.Console`, `Editor.Output`, `Editor.Scene`,
`Editor.Hierarchy`, `Editor.Properties`, and `Editor.AssetBrowser`. Their dock handles use the
stable identifiers `console`, `output`, `scene`, `hierarchy`, `properties`, and
`asset_browser`. Use the docking API when opening, closing, tabifying, or moving them.

## Console

`ConsolePanel.AddMessage(text, level)` records and displays a message. Levels are
`ConsoleLevel.Info`, `Warning`, and `Error`. `SetLevelVisible(level, visible)` changes the
display filter without losing history, `GetMessages()` returns an immutable snapshot, and
`Clear()` removes the history. Its context menu includes the platform text actions and Clear.
Connect `ContextMenuRequested(menu, global_position)` to add editor-specific commands before
the menu is displayed.

## Scene and Output

Scene and Output are intentionally black viewport placeholders. They reserve stable docking
locations for the editor renderer and game-output surface without exposing the engine's
private loop or renderer implementation.

## Hierarchy

`HierarchyPanel.AddItem(name, data, parent=None, icon=None)` adds a root or child row while
keeping arbitrary project/scene data attached to it. `GetSelectedData()` returns that data and
`Clear()` empties the tree. Listen to `SelectionChanged`, `CreateRequested`, and
`DeleteRequested`. `ContextMenuRequested` allows extensions to append commands.

## Properties

`PropertiesPanel.AddComponentSection(component_id, title, expanded=True)` returns a reusable
`ComponentSection`. Add editors with `section.AddField(label, widget)`, and control disclosure
with `SetExpanded`. Sections can be removed individually or with `Clear()`. The Add Component
button remains outside the scrolling area, so it is always accessible; listen to
`AddComponentRequested` to open the component picker. The context menu expands or collapses
every component and can be extended through `ContextMenuRequested`.

## Asset Browser

`AssetBrowserPanel.SetProjectRoot(path)` displays a safe, two-pane view: the directory tree on
the left and the selected directory on the right. Symbolic-link directories are not traversed,
and asset database `.meta` sidecars are hidden from the content view. `Refresh()` rescans the
tree, `AssetActivated(path)` reports double-clicks, and `ContextMenuRequested` supports custom
asset commands.

## Icons, themes, and localization

Panel tabs load their matching SVG from `Editor/assets/icons/dark` or `light`. A theme change
refreshes both docking styles and icons. All built-in user-facing text is resolved through
`Editor/assets/locales/en.json`; locale changes update panel captions and controls at runtime.
