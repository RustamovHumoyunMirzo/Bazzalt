# GUI stylesheets and font integration

## Ownership and ABI

FreeType 2.14.3 is pinned by commit in `get_freetype.ps1` / `.sh`, configured by
`cmake/FreeType.cmake`, and linked privately into `BazzaltGui`. It is not part of
the public SDK headers. GUI module ABI 3 extends the private dispatch table with
asset validation, font metadata, measurement, preview and CSS dependency queries.
GUI ABI 5 additionally supplies private scene-input and spatial-effect rendering hooks.
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
`GuiElement.Stylesheet` on the Frame entity scopes the root sheet. A child's `GuiElement.Stylesheet` adds a subtree
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
cache revision, with at most 8,192 glyph IDs per page. Glyph uploads use owned
buffers and occur before beginFrame. Existing spatial/camera batches protect
sampled pages from eviction. Teardown destroys batches/material instances before
their textures. Capacity or missing glyphs use the built-in fallback.

The editor preview calls the same native rasterizer and tints its alpha using
the editor palette; Qt does not independently register the font. Preview edits
are debounced. `GuiAssetWatcher` watches used GUI sources and the selected asset,
then performs targeted reimport; it does not scan the project each editor tick.
Atomic-save replacements reattach their watcher. Asset pickers filter CSS/fonts.

HarfBuzz 11.5.0 and SheenBidi 3.0.0 are pinned in `get_text_shaping.ps1` / `.sh`
and linked privately/static by `TextShaping.cmake`. FreeType remains configured
without its optional HarfBuzz auto-hinter integration, preventing circular linkage.
`FontFace::Layout` is shared by GPU text, public measurement and native preview.
It decodes UTF-8 with replacement, resolves Unicode paragraph levels and visual
line runs through SheenBidi, itemizes script runs and shapes them using HarfBuzz.
Glyph IDs (not source codepoints) key atlas entries. OpenType substitutions,
mark positioning, ligatures and kerning are therefore preserved in every Frame mode.
Neutral/inherited script characters use neighboring strong scripts. Language is
`und`, with automatic base paragraph direction and standard font features.

Wrapping accumulates logical cluster advances, breaks only between shaped clusters,
then reshapes each line with its own context and paragraph bidi levels. It is
cluster-based emergency wrapping, not full Unicode word-breaking or hyphenation.
Letter spacing is added per cluster. Size changes invalidate a four-entry bounded
layout cache per face; short static text does not reshape every editor tick.
Inputs are limited to 256 KiB / 65,536 decoded scalars and bounded glyph outputs.
No font-fallback chain or color-glyph renderer is provided; pick a font with the
required script coverage. IME/caret editing and vertical writing are separate work.
Production staging carries HarfBuzz COPYING and SheenBidi Apache license notices.
WOFF2 requires Brotli and is not advertised by the current build configuration.

## GPU CSS paint and effects

`GuiPaint.cpp` validates typed borders/radii, gradient stops and ordered filter
chains. `Cascade.cpp` expands shorthand candidates using their declaration's
priority, then resolves values after variable substitution. Longhands do not win
merely because a map visits their names last. The renderer resolves percentage
radii against actual bounds and applies CSS corner overlap reduction.

The GUI material handles antialiased elliptical corners, independently colored
and sized edges, layered linear/radial/conic gradients and premultiplied texture
copies. Filters isolate an element/subtree before applying ordered passes.
A backdrop reads the current accumulation into a distinct scratch target, then
composites a clipped result; it never samples its own render attachment. Gaussian
blur is separable; drop-shadow combines blurred alpha with the original source.
Color filters operate on straight RGB and restore premultiplication afterward.

Plans, scenes, samplers and targets are prepared before beginFrame. Slots are reused
and unused passes retired. Fonts remain protected while any planned pass samples
them. Per destination: 16M scratch pixels, dimensions <=4096, 128 targets, 256 passes
and 16 nested groups. Resolution adapts to the allocation budget. Over-limit plans
report diagnostics and rebuild ordinary unfiltered geometry.
The accumulation buffer and optional scene input retain full destination resolution;
only effect scratch buffers are reduced, so a blurred panel does not downsample
the rest of the game image. Their full-size attachments are separate from the
16M scratch-pixel budget.
Full-size effect destinations are limited to 16384 pixels per edge and 32M pixels;
larger spatial reference canvases fall back to ordinary geometry before allocating
a texture, rather than sending invalid dimensions to Filament.

Direct Game presentation captures current world rendering into a private scene
input before GUI compositing; clear it explicitly even with no camera. Capture
overlays can read their current world target while writing a separate scratch
target, then composite back. Spatial Frames render GUI locally and place the
result on their Frame plane; their backdrop is earlier plane-local GUI, rather
than an arbitrary camera-dependent world sample. Render spatial plans before
camera/world views. Core and GUI module must both use ABI 5.

The optional GPU test reads actual border/radius/gradient pixels, invert,
grayscale, blur, drop-shadow, backdrop, responsive @viewport and spatial effects.

## Extending and verifying

For a public field, update GUI.h, serialization versions/migrations, GuiBridge,
LuaGUI, picker routing, locale labels, public references and tests together.
For a CSS property, add its typed adapter, inheritance/default semantics and
layout or paint tests. Preserve authored fields and nested Frame isolation.

Run native CTest, the optional `BAZZALT_GUI_GPU_TEST=1` GUI test (actual glyph
readback), `Editor.tests.test_gui_style_assets`, Compiler dependency tests and
Launcher module-ABI tests. Refresh API snapshots with `sync_api_reference.py`,
then navigation with `build_public_docs.py`, and run both check modes.
