# GUI CSS parser and cascade library

`src/GUI/CSS` is an engine-owned, dependency-free C++20 static library named
`BazzaltCSS`. It is private, not installed as a gameplay SDK header and not exposed
as an editor/renderer API. `BazzaltGui` links it privately. Root CMake excludes its
sources from the core recursive glob, so it is never compiled twice. It can be
configured and tested independently; do not fetch/build Filament or Qt for parser work.

The parser is integrated through `GuiStyles.cpp`: imported CSS assets, Frame/subtree scopes, metadata/state snapshots, typed computed copies and editor live invalidation are implemented. See [gui-styles-fonts.md](gui-styles-fonts.md) for the integration and font resource lifecycle.

## Internal API

Include `GUI/CSS/CSS.h` inside engine code, namespace `Bazzalt::Runtime::CSS`.
All names use PascalCase. All results own their strings/tokens; no `string_view`
input or entity pointers are retained by parse/cascade results.

| Type/function | Contract |
| --- | --- |
| `Location` | Byte offset, one-based line and byte column in the UTF-8 source. |
| `Diagnostic` | Warning/error, source location and readable description. |
| `Limits` | Byte/token/rule/declaration limits; nesting/selector/expansion/ancestor/work bounds. Defaults are in the header; traversal has a hard safety ceiling of 128. |
| `Tokenize(source, diagnostics, limits)` | Owning tokens plus End sentinel. CSS comments, escapes, UTF-8, strings, hashes, at-keywords, signed/exponent numbers and dimensions/percentages. Invalid UTF-8, NUL, unterminated strings/comments and nonfinite numbers are errors. |
| `Parse(source, limits)` | Stylesheet with rules, selector ASTs, declaration values, conditions, layer order, retained at-rules and diagnostics. Recovers to inspect later input; `HasErrors()` means it must not become authoritative. |
| `ParseValue(source, limits)` | Balanced token value with diagnostics. Functions/unknown units are retained as data; not necessarily convertible to a GUI property. |
| `Value::ToString()` | Normalized CSS token serialization; not original source formatting. Strings/identifiers are escaped. |
| `ParseSelectors(source, diagnostics, limits)` | Strict selector-list AST. Unsupported/malformed selectors are diagnosed; no partial selector list silently broadens a rule. |
| `Specificity` | Tuple of IDs, classes/attributes/pseudos, and types. `:where()` contributes zero; `:is()`/`:not()` contribute the maximum argument specificity. |
| `Matches(selector, node, limits)` | Right-to-left matching; bounded ancestor/sibling/functional traversal. No live ECS reads. |
| `EvaluateCondition(condition, environment)` | GUI viewport/media feature evaluation; unknown features fail closed. |
| `Compute(sheet, node, environment, limits)` | Match/cascade for one snapshot. Parent custom properties are computed before child overrides. Highest importance/layer/specificity/source order wins. Duplicate declarations keep their original order. Syntax-invalid sheets return diagnostics and no properties. |
| `SubstituteVariables(value, variables, diagnostics, limits)` | Token-level `var(--name, fallback)` substitution. Cycle detection includes unused fallback references; cyclic variables are invalid, while noncyclic consumers may use fallbacks. Expansion depth/bytes/work are bounded. |
| `ResolveLength(value, environment)` | Optional finite logical length/scalar; units and calc/min/max/clamp below. Does not decide whether a particular property accepts negative/scalar values. |
| `ResolveColor(value, currentColor)` | Optional RGBA in encoded sRGB, not linearized rendering values. The typed GUI adapter must choose color-space conversion. |
| `ResolveString` / `ResolveBoolean` | Single quoted string/identifier; booleans accept true/false only. Invalid conversion returns no value. |
| `ResolveReference` | Extract url()/asset() data. It never opens a file, resolves UUIDs, downloads or traverses directories. Asset resolution/allowlisting belongs to AssetDatabase. |

`ComputedStyle.Properties` contains ordinary winning declarations after variable
substitution. It deliberately retains `inherit`, `initial`, `unset` and unrecognized
property names. The GUI adapter must apply a property schema, explicit inheritance,
validation and defaults. It must not reinterpret unknown values as C++ expressions.
Invalid variable expansion omits that winning declaration and adds a diagnostic;
it does not fall back to a lower-priority declaration. Custom properties are
case-sensitive; ordinary property names and pseudo/type names use ASCII folding.

## Snapshot adapter contract

`Node` is an immutable caller-owned tree snapshot, not an Entity/Component:

- Type is the GUI widget type; ID, Classes and Attributes are explicit user metadata.
- Parent and PreviousSibling must remain valid during a call. Only GUI entities in
  the same nearest Frame should be linked. Do not cross independent Frame roots.
- ChildIndex/ChildCount are one-based sibling position/count. TypeIndex/TypeCount
  are sibling position/count restricted to the same type, not number of descendants.
- States contain hover, active, focus, focus-visible, focus-within, disabled,
  checked, selected, empty, or names queried through `:state(name)`. The adapter
  owns focus-within/empty calculation and aligns `[disabled]` with `:disabled`.
- The matcher does not read SDL, perform picking or infer states from arbitrary
  attribute strings. Disabled means `:enabled` is false; all other nodes are enabled.

Do not retain these pointer graphs across scene edits. Detect hierarchy cycles in
the adapter; Compute also rejects parent cycles/depth overflow. Matching and cascade
have explicit operation limits. Cache immutable parsed stylesheets by asset revision;
later integration should not parse on every render tick or rewrite authored fields.

## Supported engine CSS dialect

Selectors: `*`, type, #id, .class; presence and =, ~=, |=, ^=, $=, *= attributes with
optional i/s flags; descendant, child (>), adjacent (+), general (~) sibling relations;
comma lists; state/root/empty/structural pseudos; An+B nth forms; :is/:not/:where lists.
Unsupported pseudo-elements, :has, CSS nesting and namespace selectors are errors.
The matcher has no DOM-only pseudo states, shadow tree or browser history.

Values: escaped strings/identifiers, numbers/units/percentages, generic balanced
function/block syntax, custom variables and asset references. ResolveLength supports
px/dp, %, em/rem, vw/vh/vmin/vmax, calc arithmetic, min/max/clamp; mixed-dimensional
addition, length*length, divide-by-zero and nonfinite results fail. PercentageBase,
FontSize and RootFontSize are supplied by the layout adapter, never guessed here.
Unitless numbers remain accepted for scalar GUI properties; the typed property
schema decides whether a particular length permits a unitless value.

Colors: #RGB/#RGBA/#RRGGBB/#RRGGBBAA, rgb/rgba (comma or space with slash alpha),
hsl/hsla angle hues, hwb, color(srgb ...), transparent/currentColor, CSS's basic
16-color palette plus grey/orange/rebeccapurple/cyan/magenta aliases. Other named
colors/spaces/gradients remain parsed data, but ResolveColor returns no value.
This is not a claim to implement all CSS Color 4/5 conversion or every named color.

At-rules: @viewport/@media conditionally contain ordinary rules; features are
min/max/exact width/height, orientation, prefers-color-scheme and
prefers-reduced-motion, with screen/all, and/or/not/comma grouping. @viewport is
an engine conditional block, not the obsolete browser viewport descriptor API.
@layer statements/blocks implement named order and important-order reversal.
Layers are flattened full paths, not full browser nested-layer ordering/revert-layer.
Anonymous layer names are private implementation identities. Other at-rules (including
@import, @font-face, @keyframes, @supports) are retained with warnings and never executed;
their nested contents never leak into the ordinary rule cascade. Imports never fetch.

## Verification and CI

```
cmake -S src/GUI/CSS -B build/css-tests
cmake --build build/css-tests --config Release
ctest --test-dir build/css-tests -C Release --output-on-failure
```

`BazzaltCSSTests` also joins normal root CTest. Checks never depend on assert/NDEBUG.
Tests cover token escapes/locations, selectors/specificity/state/attributes, typed
values, nested/cyclic/fallback variables, hierarchy inheritance, layers/important,
viewport rules, opaque at-rules, recovery/limits and 1,000 deterministic malformed
inputs. Keep the `Button { ) ] ; color:red; }` no-progress regression. Tests have a
20-second timeout. The lightweight GUI CSS workflow runs Windows x64/x86 (game-core
compatibility, not x86 editor support) and Linux with ASan/UBSan; no big dependencies.

Future work: complete stylesheet asset/component integration, typed GUI property
mapping, invalidation/hot reload, font assets/FreeType, and optional additional CSS
grammar. Do not turn warnings into silent simulated browser behavior.
