# Change recipes

## Choose the owning layer

Widget layout belongs in the editor, solver work in its provider, and gameplay
operations in public contracts plus core adapters. The [code map](maintenance-map.md)
names current integration files. Trace one working vertical slice first. Move code
with its tests before changing behavior. Preserve signatures, persisted IDs and
lifecycle order unless the feature explicitly changes them.

## Change a built-in component field

1. Define units, default, range and runtime semantics in the public header.
   Validate at runtime even when inspector rules limit input.
2. Extend the existing feature `*Fields.h` visitor when it serves codecs and
   inspection. `VisitBehaviorTreeAgent` is a small example.
3. Specify how files without the field load. Default construction often suits an
   optional new field. Keep required-data validation strict. Bump the component
   version and migrate when representation or meaning changes.
4. Update native bridge read/write descriptors. Commit edits through existing
   history/dirty paths. Follow copy-validate-assign behavior where used. Reads
   must not dirty a scene.
5. Set widget, units, range, section and picker intent. Use shared widgets,
   resources, localization and `inspector_sections.py`.
6. Bind the field in its Lua file. Check value copies, optional results and lifetime.
7. Test current/old data, invalid values, save/reload, runtime effect, inspector
   edits and undo/redo. Update C++/Lua reference and release notes.

## Add a built-in component or system

Use public `Component` and `System` contracts. Keep native handles in
runtime/provider caches. Persistent components contain values and UUIDs. Entity
handles carry generations and are transient.

Register the codec with `ComponentSerializationRegistry`. Its wrapper handles the
common enabled flag. Declare entity-reference patterns with
`RegisterEntityReference` so prefab/duplication remapping distinguishes entity and
asset UUIDs. Add/remove/inspect/edit bridge dispatch and Lua entity operations
alongside the Lua type.

Decide who installs a system. `Scene::Scene` installs scene defaults. The engine
installs host-owned runtime/render systems. Keep existing frame/fixed ordering.
Document required neighbors and implement scene-clear/destruction cleanup.

For several codecs, create `RegisterFeatureComponents(registry)` and wire it once
from `SceneSerializer`. Keep new registrations with their owning feature.
Project-defined components use the [scripting registration API](scripting.md).

## Add a method or callback

Specify phase, thread, owner, errors and lifetime. Implement behind public types
and bind the same behavior in Lua. Extend the existing dispatch when the service
lives in a provider.

Retained callbacks need an owner and native module lease. Disconnect before
destroying objects, VMs or code. Cancel while referenced services remain alive.
Test stop, reload, exceptions, self-disconnect and entity destruction. AI tests
cover explicit tokens and inferred native-script owners.

## Add an asset format

Implement/register an `AssetImporter`. Preserve `.meta` UUIDs across rename/move.
Bump importer version when cache output or dependency interpretation changes.
Bound input size and validate before mutating a live scene.

Collect serialized dependencies and report Partial for computed/runtime lookups.
Test missing/transitive references. Add browser classification, icon, creation/open
behavior, inspector and typed picker filtering. Reuse resources/localization.

Follow the format through `Compiler/content.py`, cooker and runtime loading.
Test a cooked runtime read as well as a browser preview. See
[dependency tracking](asset-dependencies.md).

## Add or replace an editor panel

Create a panel under `Editor/gui/panels` and register in `application.py` with a
stable docking ID. Reuse shared theme, menus, inputs, resources and locales.
Keep feature logic in its controller/service.

Use existing selection/history/runtime interfaces. Preserve selection on refresh,
disconnect old signals when rebinding, and debounce expensive scans. Handle old
saved layout IDs on removal. Verify theme changes, narrow widths, loading and
close with unsaved edits. See [panels](editor-panels.md) and [services](editor-services.md).

## Replace a backend

Use [module providers](module-development.md) for native module backends. Keep
public values, dispatch contracts and persisted formats while replacing internal
algorithms/resources. Translate backend-specific data at the adapter.

Rendering also has core-owned Filament materials, render assets and GUI target
integration. Replacing that renderer requires adapting those contracts together.
The provider seam alone does not make every Filament dependency interchangeable.
Trace `RenderBackend`, render systems/assets and GUI dispatch ownership first.

## Remove or rename behavior

Search headers, Lua, type IDs, bridge descriptors, locales, settings, compiler
paths, tests and docs. Source renames and serialized type renames have different
compatibility requirements. Keep readers/migrations for old names and preserve
unresolved components when a feature is unavailable.

Add a focused migration test before removing compatibility code. Explain which
versions/files it covers. Use the [verification guide](verification.md) to select
checks and record actual coverage.
