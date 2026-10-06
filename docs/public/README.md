# Public documentation

Open `index.html` in a browser, or serve this directory with any static HTTP
server. The committed HTML, CSS, and JavaScript are the complete site: no CDN,
remote fonts, fetch requests, application server, or package install is needed.
Navigation, search, and theme switching also work with `file://` URLs. File-mode
clipboard permissions vary by browser; Copy falls back to selection when needed.

## Build and validate

From the repository root:

```sh
python scripts/build_public_docs.py
python scripts/build_public_docs.py --check
python scripts/public_api_docs.py --check
python -m unittest discover -s tests -p test_public_docs.py -v
```

Optional browser smoke tests use an existing PySide6 WebEngine installation:

```sh
python -m unittest discover -s tests -p test_public_docs_web.py -v
```

They load the site with local file URLs, exercise search/theme/navigation, and
check desktop/mobile layout without loading the native engine.

The builder uses Python's standard library. It reads `catalog.json`, source
fragments under `_content`, and the shared `templateExample.html`. It writes
complete static documents and `assets/catalog.js` (the offline search index).
Commit generated output together with its sources. Do not edit generated guides.
The landing `index.html` and shared CSS/JS are authored directly.

## Nested navigation

Each catalog node may have `children` containing more nodes. There is no fixed
depth limit in the UI. Nodes with an `id` are pages; nodes with only a `title`
are expandable folders. A page may also have children. Ancestors of the current
page open automatically; other branches can be expanded with the disclosure
arrow. The flat `pages` array on existing top-level groups remains supported.

```json
{"title":"Editor", "children":[
  {"id":"manual/workspace", "title":"Workspace", "children":[
    {"title":"Panels", "children":[
      {"id":"manual/panels/hierarchy", "title":"Hierarchy"}
    ]}
  ]}
]}
```

## API reference maintenance

`_api/0.5.0.json` freezes the reviewed API inventory for that documentation
version. Native declarations are extracted from public headers; Lua entries
are extracted from actual registrations and binding wrappers, not copied
wholesale from C++. Private Detail/Runtime helpers are excluded. Module ABI
contracts are marked as tooling, not ordinary gameplay operations.

When changing the public API, update `scripts/public_api_notes.py` with type,
member, units, lifetime/error and parameter explanations, and update wrapper
notation/examples in `scripts/public_api_docs.py` where needed. Missing
descriptions fail the reference build. Then run:

```sh
python scripts/public_api_docs.py --version 0.5.0
python scripts/build_public_docs.py
```

Review the snapshot diff as well as the rendered documents. The extractor is
designed for this repository's declaration style, not every possible C++ syntax;
new syntax/binding helper patterns require extraction and coverage-test updates.
Coverage tests check the current snapshot against public headers/bindings. Add
new snapshots for new engine versions instead of regenerating archived versions
from future headers. A catalog node with `reference: "cpp"` or `reference: "lua"`
expands the corresponding type tree and generates its full reference pages.
Beginner/manual prose stays authored HTML, separate from the reference inventory.

## Paths and versions

```text
index.html                             Landing page: language only, no version selector
catalog.json                           Published versions, languages and guide order
templateExample.html                   Shared guide layout, not an API example
assets/                                Shared, local CSS/JS; generated search index
_content/0.5.0/native/materials/en.html  Editable article fragment
0.5.0/native/materials/en.html           Generated full document
```

A version directory is an explicit documentation snapshot. Version 0.5.0 is
the current editor/core version in `releases/versions.json`; it is not Hub's
version. To publish another version, add it to `catalog.json`, provide its
sources, choose `defaultVersion`, and rebuild. Never silently overwrite an
old snapshot with documentation for a different release. The guide version
selector preserves the page and an available anchor, falling back to the
version's introduction if that page does not exist. Only published versions
appear—no fictional upcoming/legacy choices.

Shared CSS/JS must remain compatible with all checked-in versions. For a future
incompatible documentation UI, version those shared resources as well.

## Add a guide

1. Add a unique page ID/title under a group in `catalog.json`.
2. Add `_content/<version>/<page-id>/en.html` with exactly one `h1`, an introductory
   paragraph, and stable `id` attributes on every `h2`/`h3`.
3. Write ordinary relative HTML links. They resolve from the *published* page's
   directory, not the `_content` directory. Heading links use real `#anchors`,
   so opening a link, reloading, and Back do not need a custom router.
4. Rebuild and validate. Sidebar navigation, contents, previous/next links,
   page descriptions, and full-text search data are generated.

The builder intentionally accepts trusted repository HTML, not user-supplied
content. Explain terminology before using it. Check examples against current
public headers/bindings. Do not publish private engine-loop APIs or planned
features as working gameplay functionality.

## Add a language

Add its language code/name to `catalog.json`, then add sibling files such as
`fr.html` next to the corresponding `en.html` source fragments. The filename
is the language; do not use query strings or a network translation service.
Untranslated pages are not offered as translated pages. An English copy is
required for every page; no empty translation stubs are published.

To translate the landing page, add `_locales/<language>.json` with the same
keys as the `data-i18n` text in `index.html`. English landing text is extracted
from the HTML, so it has one source of truth. Guide titles and contents come
from their own language files. Translate shared template labels for a new
language as part of that change; currently only English is published.

Search is scoped to the selected version and language. Language switches stay
on the same page where a translation exists. Preferences are best-effort local
storage; the site remains usable when storage is blocked. JavaScript is an
enhancement: ordinary guide content, links, navigation, and contents are static.

## Hosting and offline copies

Publish this folder as the static site root. For a local HTTP preview:

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory docs/public
```

For an offline distribution, copy the generated version folders, `index.html`,
and `assets` together. `_content`, the catalog source, template, and this README
are authoring files and can be omitted from that distribution. A service worker
is not required to open downloaded files. Automatically caching a hosted site
for disconnected revisits is a separate feature, not promised by this setup.
