# Editor panels

## Performance boundaries

Resource icons are validated and decoded once per `ResourceManager`, then shared
through Qt's implicit-sharing cache. Panel title/icon changes are batched into one
docking rebuild. Applying an unchanged theme does not restyle or rebuild the UI;
initial hierarchy population is explicit rather than relying on a theme event.

Entity selection identifies scene rows from cached scene IDs instead of serializing
all loaded scenes. Per-tick gizmo tracking uses the private `entity_pose` bridge
query, not a complete component snapshot. Stats use `entity_count`, and Edit action
availability uses the hierarchy's cached active-entity count. Hierarchy refreshes
avoid resubmitting unchanged native visibility/lock sets within the same scene.
Idle material checks reuse the last scene snapshot; authoring changes invalidate
it, and checks defer while flying or dragging. Play-mode checks still inspect
runtime changes. These optimizations do not cache editable inspector values.

The manual `Editor.tests.native_model_viewport_smoke --editor` tool measures editor
tick time using a temporary project. Warm selected-model ticks measured roughly
2–6 ms on the development RTX 3050 Ti, but the first Vulkan render still took about
1.5 seconds. Tick time excludes subsequent Qt event/paint processing and is not a
claim of end-to-end FPS. The first-render/shader warm-up stall remains a separate
startup performance limitation.

## Hierarchy helpers and model icons

The three-dot menu beside Hierarchy search offers Expand/Collapse All,
Expand/Collapse Selected, Reveal Selected, and Clear Search. Newly inserted
object groups start collapsed. Preferences > General > Expand newly added
hierarchy objects changes that default. Manual expansion choices survive refresh;
scene headers initially stay open, and collapsed scene headers stay collapsed.
Reparenting no longer forcibly opens the drop target unless that preference is on.

Hierarchy helpers and Inspector component options share `OptionsButton`, using
`icons/dark/menu_dots.svg` or `icons/light/menu_dots.svg` and refreshing on palette
changes. The button has no text-glyph fallback masquerading as a menu icon.

Asset Browser generates isometric geometry thumbnails asynchronously for GLB,
glTF, and OBJ. glTF node transforms and base material colors are honored; textures,
skin poses, sparse accessors, and compressed geometry are not previewed by this
lightweight renderer. Other formats and unreadable geometry retain their model
icon. Thumbnail generation never touches the active scene or native swapchains.
Work is bounded, uses one worker, and caches 128 results per session; source file
changes invalidate the cache key. Icons update in place without changing folder,
selection, or item size. External glTF buffers must stay inside the model folder.
Previews are static PNG images, never live viewport widgets. They are also stored
under the cross-platform app-data `BAZZALT/data/Cache/ModelThumbnails` directory.
A new cache instance loads an existing image in its worker without rendering the
model again. Source fingerprints and thumbnail format version invalidate old
images; a read-only or missing cache falls back to generating the thumbnail.
Selecting an asset does not trigger an asset-database scan/import. Filesystem
changes still mark the normal idle import workflow dirty.

Transform history uses compact before/after local-transform records for inspector
transform edits, gizmo drags, multi-selection center moves, and Object placement
commands. Undo/redo applies validated entity batches in place: no temporary scene
file, registry replacement, model reload, or hierarchy rebuild is needed. Parent
and child local values restore independently and preserve their relationship.
Structural and other snapshot commands retain the existing full-scene path;
they are not yet universally incremental. Filament keeps bounded caches of 128
material definitions and 512 program specializations to reduce repeat compilation
when scene resources are rebuilt. Cold first-render latency is still outstanding.

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

The `Search` field filters names across all loaded scenes, case-insensitively.
Space-separated terms must all match. Matching child rows retain and expand
their ancestor branches; a matching parent keeps its children visible. Search
does not change scene/entity selection, and clearing it restores the previous
expansion state. Filters survive hierarchy rebuilds, renames, and theme updates.

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

Show in File Explorer (Reveal in Finder on macOS) reveals selected assets in the
desktop file manager. Windows uses [native multi-item selection](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems), grouping files by containing folder.
Linux uses the file manager's D-Bus ShowItems interface, falling back to opening
containing folders when that desktop does not support selection. Filenames are
passed without shell interpolation; Unicode names are supported.

The `Search` field searches the whole project asset root by relative path,
filename, and extension, not only the current folder. Search is debounced to
avoid rescanning on every keystroke. Results keep the normal icons, drag/drop
payloads, activation, and context actions; tooltips disambiguate identical names
using relative paths. Metadata sidecars and symlinks are excluded. Clearing the
query restores the current folder. Refreshing with a newly created or renamed
asset exits search and reveals/selects that asset in its containing folder.

### External application opening

Double-click a `.cpp` asset, or choose **Open** from its context menu, to open it
in an external application. On Windows the first use presents the actual native
**Open With** application list, not File Explorer or an executable-file browser.
When the chosen application can be identified, Bazzalt remembers it per extension
in the editor's existing app-data settings. **Open with…** always shows the chooser;
canceling preserves the previous choice. A missing/uninstalled application prompts
again. Selecting multiple C++ assets chooses once and passes the remaining files
to the identified app; Windows itself has already opened the first file. If no
handler can be identified, each remaining file gets its own native chooser.
Folders, scenes, models, and other asset types retain their existing behavior.

Associations are editor-local and never modify OS defaults. Windows' native
[Open With shell API](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenwithdialog)
does not return the selected application. Bazzalt optionally resolves newly
observed Windows Open With history into a validated executable path. Unchanged
or unavailable history is not treated as proof of the user's selection: the app
is left uncached and the next Open prompts again. A successful but unidentified
replacement also clears an old cached choice. No OS defaults are changed.
macOS and Linux currently retain the native application-file browser fallback.
Arguments are passed without a command shell;
Unicode filenames and spaces are preserved. Launch failures appear in Console and
are not saved as successful choices. A successful process launch does not guarantee
that the application itself will accept the file.

`Editor.asset_opening.ExternalAssetOpener` owns this policy separately from import
and compilation. `RegisterExtension(".hpp")` enables another format, or supply an
explicit `extensions` collection when constructing the service. New formats must
be opted in; unsupported project files are never executed automatically.

### File Association preferences

**Edit → Preferences → File Associations** lists supported formats and their saved
applications (hover for the full path). By default **Open** and double-click reuse
the saved program; if no valid program is saved they prompt. **Open with…** always
prompts, even when a saved program exists. Successful identifiable selections replace
the saved association; cancellation never does.

- **Remember applications** enables cached opening. When off, each Open prompts;
  existing saved choices are retained and new choices are not saved.
- **Set Application…** explicitly assigns an executable using native program-file
  browsing. This also provides a reliable override when Windows cannot report a
  choice from its native application list. It does not launch a project file.
- **Reset Selected** forgets the chosen extension. **Reset All** forgets every
  saved association, so the next Open prompts again.
- **Restore Defaults** restores remembering and clears associations.

All preference edits, including resets, are staged until **Apply**. **Cancel**
does not change saved associations. Settings use the existing cross-platform editor
app-data JSON store and survive editor restarts; no OS default associations are
modified. The service additionally exposes `Associations()`,
`SetAssociation(extension, application)` and `ResetAssociations(extension=None)`
for editor integrations. These operations preserve unrelated editor settings.

## Compact field sizing

Single-line text, numeric, and dropdown inputs use the shared theme's
`control_height` of 20 logical pixels, with matching maximum heights. Inspector
fields use an 18-pixel content height; borders add to the final widget height.
Multiline editors, tree/list rows, and component headers retain their own
layout rules. Both dark and light themes use the same sizing tokens.

## Icons, themes, and localization

Panel tabs load their matching SVG from `Editor/assets/icons/dark` or `light`. A theme change
refreshes both docking styles and icons. All built-in user-facing text is resolved through
`Editor/assets/locales/en.json`; locale changes update panel captions and controls at runtime.
## Scene inspection and protected entities

Selecting exactly one scene row opens its scene properties, including Environment,
and hides Add Component. Scene rows are not ECS entities and cannot receive components.
Hierarchy refreshes preserve the inspected scene. Mixed scene/entity selections
operate on the entities, not on the scene roots.

Add Component starts with a localized search input. Matching is case-insensitive
and includes built-in and discovered script components. Components already attached
to the entire selection stay disabled; clearing search restores the list.

Each entity row reserves two compact columns at its right edge: the themed lock
indicator (`icons/dark/lock.svg` or `icons/light/lock.svg`) and Scene visibility.
The lock column is indicator-only: unlocked entities have no icon or invisible
button. Use Lock/Unlock in Editor from the context menu. Click the visibility
column or use Hide/Show in Scene View from the context menu.
A locked entity cannot be selected, inspected, renamed,
reparented, deleted, transformed, or edited through component controls. Protection
inherits through descendants; unlock an ancestor to release inherited protection.
Deleting a parent that contains locked descendants is rejected.

Visibility is independent of locking. Hidden rows are dimmed; their Scene geometry,
light contribution, icons, picking, marquee selection, outlines, and manipulation
handles are suppressed. They can still be inspected from Hierarchy unless locked.
Game cameras and Game rendering keep the original component visibility and light
settings. The renderer temporarily suspends hidden resources for Scene rendering,
then restores precisely the resources it removed before rendering Game views.

These are editor authoring preferences, not runtime components or `.bscene` data.
They autosave by project UUID and entity UUID in editor settings and do not dirty
the scene, become undo records, or affect gameplay scripts. The public engine API
does not expose editor selection locks. Duplicating an entity creates an unlocked,
visible copy.
## Thumbnail worker ownership and CI reuse

Model previews are static images, not live 3D viewports. A bounded single-worker
Python executor reads or renders cached PNGs off the GUI thread. A GUI-owned
timer collects completed futures and emits the public Ready signal on the GUI
thread; workers never call QObject signals. The timer stops when idle. QPixmap
and browser icon updates stay on the GUI thread. Cache destruction cancels queued jobs without blocking the UI and
suppresses results from running jobs.

Thumbnail timeout assertions include worker states, timer activity and unexpected
future exceptions via WorkerDiagnostics, distinguishing execution from UI delivery
failures. Tests also cover workers completing before the GUI pumps events.

Win32 CI uses exact dependency caches first. Cache misses optionally fetch the
known Qt and Filament artifacts from run 37308449117. Expired/missing artifacts
fall back to normal builds. The dependency build script validates pinned source,
compiler, options and source-patch stamps before skipping compilation. Large
dependency builds remain in GitHub Actions.
