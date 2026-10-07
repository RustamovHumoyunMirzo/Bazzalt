# Editing the docs

**Edit the HTML page you open in the browser. There is no separate content folder.**

```text
docs/public/
  index.html                  Home page
  site.json                   Optional section names and reading order
  templateExample.html        Shared page layout
  assets/                     Shared CSS/JS; automatic search index
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

Only existing translations appear in a guide's language selector. Untranslated
home-page destinations fall back to the default-language guide for that subject,
not an unrelated translated page. Native language names come from browser Intl.

Optionally translate the home page in one file:
`assets/languages/ru.json`. Use its `data-i18n` keys, such as `title`, `intro`, and
`manualTitle`; omitted keys keep the English home-page text. This file is not a
language registry and is not needed to add a translated guide. Guide contents
remain independent language HTML files.

## Add a version

Copy `0.5.0` to `0.6.0`, edit that version's pages, and refresh. **The folder name
is the version declaration.** No version list needs updating. Use semantic names
such as `0.6.0` or `0.6.0-beta.1`. The highest version is the home-page default;
each guide's selector includes discovered version folders. Old versions remain
ordinary editable files, not a duplicate generated/source tree.

## Refresh and check

From the repository root:

```sh
python scripts/build_public_docs.py
python scripts/build_public_docs.py --check
python -m unittest discover -s tests -p test_public_docs.py -v
```

The first command refreshes the shared layout, relative URLs, selectors,
table of contents, previous/next links, and `assets/catalog.js` navigation/search
index. The sidebar is rendered once from that shared local index, not copied
into every page. Adding a guide updates the shared index and adjacent pager
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
public folder for an offline edition. To preview over local HTTP:

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory docs/public
```
