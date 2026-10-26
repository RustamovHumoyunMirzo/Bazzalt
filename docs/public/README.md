# Editing the docs

**Edit the HTML page you open in the browser. There is no separate content folder.**

```text
docs/public/
  index.html                  Home page
  site.json                   Optional section names and reading order
  templateExample.html        Shared page layout
  assets/                     Shared CSS/JS and generated data bundles
  0.5.0/                      One folder per documentation version
    manual/
      en.html                 Edit this page directly
      first-launch/
        en.html
      panels/
        hierarchy/
          en.html
          ru.html             Add this file to translate this page
    native/reference/Scene/en.html
    lua/reference/Scene/en.html
```

## Change a page

Open its HTML file and edit inside:

```html
<article class="doc">
  <h1>Page title</h1>
  <p>Introduction.</p>
  <h2 id="topic">Topic</h2>
  <p>Your explanation.</p>
</article>
```

This article is the single source of truth. The refresher never replaces it
with another source or an API snapshot. Text in the page header/sidebar/footer
comes from the shared template and is refreshed automatically. Keep one `h1`
and stable, unique `id` attributes on every `h2`/`h3`.

## Keep What's New current

When changing a public API or user-visible editor behavior, update the current
release's `whats-new/en.html` in the same change. Explain what users can now do,
what changed, and whether existing code or scenes need migration. Link API
additions to both C++ and Lua documentation and editor changes to the relevant
manual. Keep refinements with their existing feature entry and preserve older
release notes. Run the refresh/check commands below so navigation, headings and
search include the updated notes.

## Add a page or subpage

Copy an existing HTML page into a new folder, for example
`0.5.0/manual/new-guide/en.html`, and replace its article. Run the refresh command
below. The folder automatically appears in navigation and search; no page list
registration is required. Subfolders are sub-navigation, at any depth.

## Add a language

Copy a page's `en.html` to `ru.html` (or `uz.html`, `fr.html`, `en-US.html`, etc.)
in the same folder, translate its article, and refresh. **The filename is the
language declaration.** No language list needs updating. The document's `lang`
attribute, links, selector, and search entries are derived from it.

Version and language selectors are populated from the shared catalog, not
written into each HTML page. A guide's version comes from its directory and
its language from its filename at runtime. No `data-version`, `data-page`,
versioned title/footer, or copied lists of languages need maintaining.
Only existing translations appear in a guide's language selector. Untranslated
home-page destinations fall back to the default-language guide for that subject,
not an unrelated translated page. Native language names come from browser Intl.

Optionally translate the home page in one file:
`assets/languages/ru.json`. Use its `data-i18n` keys, such as `title`, `intro`, and
`manualTitle`; omitted keys keep the English home-page text. This file is not a
language registry and is not needed to add a translated guide. Guide contents
remain independent language HTML files.

## Add a version

Run one command, then edit only the new release's content:

```sh
python scripts/build_public_docs.py --new-version 0.7.0
# Or copy a particular release:
python scripts/build_public_docs.py --new-version 0.7.0 --from-version 0.6.0
```

The command copies an editable snapshot and refreshes the catalog. It refuses
to overwrite an existing version. You can also copy a version folder manually
and run the ordinary refresh command. **The folder name is the version
declaration.** No version list needs updating. The highest version is the
home-page default. Historical articles remain independent: do not share mutable
article files between releases, or a new API edit would rewrite older docs.

Adding a version or translation updates the small `assets/catalog.js` manifest
and its generated navigation/search bundles, plus the new HTML files,
not the selectors/footer/title of every existing page. Previous/next links are
relative within a release, so copying it does not leave links to the old one.
Articles with deliberately written release-specific examples or links remain
your authored content; review those when preparing a new release.

## Refresh and check

From the repository root:

```sh
python scripts/build_public_docs.py
python scripts/build_public_docs.py --check
python -m unittest discover -s tests -p test_public_docs.py -v
```

The first command refreshes the shared layout, relative URLs,
table of contents, previous/next links, and generated data under `assets/`.
`catalog.js` contains only release/language manifests and home-page destinations.
Guides load one `assets/navigation/<version>/<language>.js` bundle; the home page
loads no navigation bundle. Full-text data in
`assets/search/<version>/<language>.js` loads only when search opens and is reused
for subsequent searches. Search results are limited to 60 visible entries; refine
a query to find more matches. These generated scripts work with both HTTP and
`file://`, without fetching unrelated releases on startup. Do not edit them by
hand. Adding a guide updates its release bundles and adjacent pager
links; unrelated pages no longer change merely because the tree changed.
Commit refreshed files. The article text remains unchanged. No
packages or engine compilation are needed.

Optional UI checks with an existing Qt WebEngine installation:

```sh
python -m unittest discover -s tests -p test_public_docs_web.py -v
```

## Navigation names and order

`site.json` is the only optional navigation configuration. It gives existing
folders friendlier names and prioritizes an initial reading order. New pages,
sections, languages, and versions are still found without adding them here.
Pages not listed in an order array follow alphabetically. The page's `h1`
provides its label; a folder's nesting provides its children.

## Code examples

Use `<pre><code class="language-lua">...</code></pre>` (or `language-cpp`,
`language-c`, `language-bshader`, `language-glsl`, `language-json`, `language-yaml`).
The local script adds a language label, Copy button, and syntax colors; it does
not download a highlighter. Mark terminal output as `language-text` or shell
commands as `language-bash`. Clipboard copying preserves the original text.

## API reference and offline use

API reference pages are now normal HTML too. Edit the relevant C++ or Lua type
page when its API changes; there is no special snapshot/description pipeline.
Preserve the distinction between actual Lua bindings and C++ declarations.

Open `index.html` directly, or serve this folder as a static site. All resources
are local; navigation and search work offline with file URLs. Copy the entire
public folder for an offline edition. JavaScript is required for selectors,
navigation/search and home-page destinations, but never requires a server or
network fetch; article text and ordinary relative links remain readable without
JavaScript. To preview over local HTTP:

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory docs/public
```
