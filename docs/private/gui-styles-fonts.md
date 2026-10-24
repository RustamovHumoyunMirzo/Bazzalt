# GUI stylesheets and font integration

## Ownership and ABI

FreeType 2.14.3 is pinned by commit in `get_freetype.ps1` / `.sh`, configured by
`cmake/FreeType.cmake`, and linked privately into `BazzaltGui`. It is not part of
the public SDK headers. GUI module ABI 3 extends the private dispatch table with
asset validation, font metadata, measurement, preview and CSS dependency queries.
Deploy matching core/GUI binaries. Production staging includes FreeType and its
bundled zlib license notices. Font distribution rights remain the game's concern.

`Font.h` is a value handle (UUID + collection face), not an FT_Face wrapper.
The core facade dispatches into the module; C++ and Lua have the same contract.
Font properties in native and Lua scripts use the existing typed asset pipeline.
No gameplay caller creates a font library or accesses renderer handles.

## Import and cooking

`GuiDataImporter` validates `.css` and supported font formats before caching.
FreeType reads bounded memory loaded from filesystem paths, avoiding ANSI path
conversion for Unicode project folders. Fonts must expose a Unicode charmap.
Font sources are limited to 32 MiB, 256 faces and 200,000 glyphs. CSS sources
are limited to 1 MiB and the parser's work/token/depth limits.

The CSS importer writes `.dependencies.yml` beside its cached product. Reference
discovery walks parsed declarations (including variable fallbacks), never comments
or unsupported at-rule bodies. `Compiler/content.py` retains those UUIDs and the
manifest, and rejects excluded required dependencies. No URL fetch or filesystem
path escape is implemented. Failed assets retain import errors for the browser
warning icon and inspector; they do not terminate the editor.

## Computed styles

`GuiStyles.cpp` builds immutable hierarchy/state snapshots inside each Frame.
`Frame.Stylesheet` scopes the root sheet. `GuiElement.Stylesheet` adds a subtree
scope, represented by a private selector state with unchanged specificity.
Nested Frames are independent. Metadata includes ID (default entity name),
Classes, Tags and presence/exact Attributes. Actual input supplies hover, active,
focus and focus-within; controls and metadata supply disabled/checked/selected.

`GuiComputed` stores component copies and the cascade result. Typed adapters write
only these copies. Authored serialization, undo and editor fields remain unchanged.
CSS overrides clear only the affected computed Inherit flag. Layout resolves
percentages against actual parent content bounds during traversal, and resolves
font-relative units with the effective font context. `display:none` removes bounds
and descendants; `pointer-events:none` prevents focus/hit/capture. Opacity affects
an entity's own layers, not a group-composited intermediate surface.

No stylesheet means no CSS snapshot work. Parsed sheets cache by asset revision;
metadata caches are bounded. Limits include 16 scopes, 10,000 combined rules,
4,096 nodes, 128 levels and bounded cascade work. Changed diagnostics go to the
editor Console and `GUI::GetStyleDiagnostics`; unsupported properties are visible.
See the public stylesheet guide for the exact mapped-property subset.

## FreeType rendering and previews

`FontFace` owns its FT_Library, FT_Face and source bytes in destruction-safe order.
It selects scalable sizes or the nearest bitmap strike, and exposes glyph metrics,
grayscale/monochrome alpha and basic pair kerning. CPU metadata/measurement/preview
uses a bounded eight-face cache. Invalid handles return empty metadata/zero size.

The renderer caches up to 16 RGBA 1024-square pages by UUID, face, pixel size and
cache revision, with at most 8,192 codepoints per page. Glyph uploads use owned
buffers and occur before beginFrame. Existing spatial/camera batches protect
sampled pages from eviction. Teardown destroys batches/material instances before
their textures. Capacity or missing glyphs use the built-in fallback.

The editor preview calls the same native rasterizer and tints its alpha using
the editor palette; Qt does not independently register the font. Preview edits
are debounced. `GuiAssetWatcher` watches used GUI sources and the selected asset,
then performs targeted reimport; it does not scan the project each editor tick.
Atomic-save replacements reattach their watcher. Asset pickers filter CSS/fonts.

FreeType provides rasterization and pair kerning, not complex-script shaping,
bidirectional layout or a fallback-font chain. Those are explicit future features.
WOFF2 requires Brotli and is not advertised by the current build configuration.

## Extending and verifying

For a public field, update GUI.h, serialization versions/migrations, GuiBridge,
LuaGUI, picker routing, locale labels, public references and tests together.
For a CSS property, add its typed adapter, inheritance/default semantics and
layout or paint tests. Preserve authored fields and nested Frame isolation.

Run native CTest, the optional `BAZZALT_GUI_GPU_TEST=1` GUI test (actual glyph
readback), `Editor.tests.test_gui_style_assets`, Compiler dependency tests and
Launcher module-ABI tests. Refresh API snapshots with `sync_api_reference.py`,
then navigation with `build_public_docs.py`, and run both check modes.
