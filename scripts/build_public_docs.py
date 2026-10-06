"""Build/check the offline-capable public docs using only Python's standard library."""
from __future__ import annotations

import argparse
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re
try:
    from scripts.public_api_docs import reference_nodes, render as render_reference, slug
except ModuleNotFoundError:
    from public_api_docs import reference_nodes, render as render_reference, slug

DEFAULT_ROOT = Path(__file__).resolve().parents[1] / "docs" / "public"


class Content(HTMLParser):
    def __init__(self, source: str):
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.headings: list[dict] = []
        self.text: list[str] = []
        self.paragraphs: list[str] = []
        self.links: list[tuple[str, str]] = []
        self.title = ""
        self._heading = None
        self._paragraph = None
        self.feed(source)
        if not self.title:
            raise ValueError("Each document needs a non-empty h1 title")

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            identifier = attrs["id"]
            if identifier in self.ids:
                raise ValueError(f"Duplicate anchor: {identifier}")
            self.ids.add(identifier)
        if tag in ("h1", "h2", "h3"):
            if tag != "h1" and not attrs.get("id"):
                raise ValueError("Every h2/h3 needs a stable id for deep links")
            self._heading = {"level": tag, "id": attrs.get("id", ""), "text": ""}
        if tag == "p":
            self._paragraph = ""
        for attribute in ("href", "src"):
            if attribute in attrs:
                self.links.append((tag, attrs[attribute]))

    def handle_data(self, text):
        self.text.append(text)
        if self._heading is not None:
            self._heading["text"] += text
        if self._paragraph is not None:
            self._paragraph += text

    def handle_endtag(self, tag):
        if self._heading is not None and tag == self._heading["level"]:
            self._heading["text"] = self._heading["text"].strip()
            if tag == "h1":
                if self.title:
                    raise ValueError("A document must have exactly one h1")
                self.title = self._heading["text"]
            else:
                self.headings.append(self._heading)
            self._heading = None
        if tag == "p" and self._paragraph is not None:
            self.paragraphs.append(" ".join(self._paragraph.split()))
            self._paragraph = None


class LandingTexts(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.values = {}
        self.key = None
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        key = dict(attrs).get("data-i18n")
        if key:
            self.key = key
            self.values[key] = ""

    def handle_data(self, data):
        if self.key:
            self.values[self.key] += data

    def handle_endtag(self, tag):
        self.key = None


def safe_id(value: str, path=False):
    pattern = r"[a-zA-Z0-9][a-zA-Z0-9._-]*(?:/[a-zA-Z0-9][a-zA-Z0-9._-]*)*" if path else r"[a-zA-Z0-9][a-zA-Z0-9._-]*"
    if not re.fullmatch(pattern, value) or any(part in (".", "..") for part in value.split("/")):
        raise ValueError(f"Invalid documentation identifier: {value}")
    return value


def flatten_pages(nodes, trail=()):
    """A node may be a page, a folder, or a page with any depth of children."""
    for node in nodes:
        title = node["title"]
        if "id" in node:
            yield dict(node, group=" / ".join(trail), children=None)
        yield from flatten_pages(node.get("children", []), trail + (title,))


def render_tree(nodes, available, prefix, current):
    by_id = {p["id"]: p for p in available}
    parts = []
    for node in nodes:
        page = by_id.get(node.get("id"))
        children = render_tree(node.get("children", []), available, prefix, current)
        if not page and not children:
            continue
        def page_link(p):
            active = ' aria-current="page"' if p["id"] == current else ''
            return f'<a class="nav-link" href="{prefix}{p["url"]}"{active}>{html.escape(p["title"])}</a>'
        if children:
            descendants = {p["id"] for p in flatten_pages(node.get("children", []))}
            opened = ' open' if current in descendants or (page and page["id"] == current) else ''
            parts.append(f'<details{opened}><summary>{html.escape(node["title"])}</summary><div class="tree-list">')
            if page:
                parts.append(page_link(page))
            parts.append(children + '</div></details>')
        elif page:
            parts.append(page_link(page))
    return '\n'.join(parts)


def outputs(root: Path) -> dict[Path, str]:
    catalog = json.loads((root / "catalog.json").read_text(encoding="utf-8"))
    template = (root / "templateExample.html").read_text(encoding="utf-8")
    languages = catalog["languages"]
    if "en" not in languages:
        raise ValueError("English fallback is required")
    versions = catalog["versions"]
    ids = [safe_id(v["id"]) for v in versions]
    if len(set(ids)) != len(ids) or catalog["defaultVersion"] not in ids:
        raise ValueError("Versions must be unique and defaultVersion must exist")
    for language in languages:
        safe_id(language)
    records = []
    result = {}
    reference_bodies = {}
    for version in versions:
        # API data is a versioned snapshot, never regenerated from future headers
        # while building an archived version of the site.
        snapshot_path = root / '_api' / f'{version.get("apiVersion", version["id"])}.json'
        api = json.loads(snapshot_path.read_text(encoding='utf-8')) if snapshot_path.exists() else None
        def expand(nodes):
            for node in nodes:
                if node.get('reference'):
                    if api is None: raise ValueError(f'Missing API snapshot: {snapshot_path}')
                    lang = node['reference']
                    node['children'] = reference_nodes(api, lang)
                    for type_ in api[lang]:
                        identifier = ('native' if lang == 'cpp' else 'lua')+'/reference/'+slug(type_['name'])
                        reference_bodies[(version['id'],identifier)] = render_reference(type_,lang)
                expand(node.get('children',node.get('pages',[])))
        expand(version['groups'])
        nodes = [dict(g, children=g.get("children", g.get("pages", [])), id=None) for g in version["groups"]]
        for node in nodes:
            node.pop("id")
        pages = list(flatten_pages(nodes))
        page_ids = [safe_id(p["id"], path=True) for p in pages]
        if len(set(page_ids)) != len(page_ids):
            raise ValueError("Duplicate page identifier")
        for language in languages:
            for page in pages:
                path = root / "_content" / version["id"] / page["id"] / f"{language}.html"
                reference_body = reference_bodies.get((version['id'],page['id'])) if language == 'en' else None
                if not path.is_file() and reference_body is None:
                    if language == "en":
                        raise ValueError(f"Missing English source: {path}")
                    continue  # Untranslated pages are not offered as translations.
                body = reference_body if reference_body is not None else path.read_text(encoding="utf-8").strip()
                parsed = Content(body)
                records.append(dict(version=version["id"], language=language, id=page["id"],
                    title=parsed.title, group=page["group"], description=(parsed.paragraphs or [parsed.title])[0],
                    text=" ".join(" ".join(parsed.text).split()), headings=parsed.headings,
                    anchors=sorted(parsed.ids), url=f'{version["id"]}/{page["id"]}/{language}.html', body=body))
    for record in records:
        v, lang, page_id = record["version"], record["language"], record["id"]
        available = [p for p in records if p["version"] == v and p["language"] == lang]
        prefix = "../" * (len(page_id.split("/")) + 1)
        link = lambda p: prefix + p["url"]
        groups = next(x for x in versions if x["id"] == v)["groups"]
        nodes = [dict(title=g["title"], children=g.get("children", g.get("pages", []))) for g in groups]
        nav = render_tree(nodes, available, prefix, page_id)
        position = available.index(record)
        pager = []
        for offset, label in ((-1, "Previous"), (1, "Next")):
            if 0 <= position + offset < len(available):
                p = available[position + offset]
                pager.append(f'<a class="{label.lower()}" href="{link(p)}"><small>{label}</small>{html.escape(p["title"])}</a>')
        version_options = ''.join(f'<option value="{x["id"]}"{" selected" if x["id"] == v else ""}>{html.escape(x["label"])}</option>' for x in versions)
        page_languages = [l for l in languages if any(p["version"] == v and p["id"] == page_id and p["language"] == l for p in records)]
        language_options = ''.join(f'<option value="{l}"{" selected" if l == lang else ""}>{html.escape(languages[l])}</option>' for l in page_languages)
        toc = ''.join(f'<a class="toc-link{" sub" if h["level"] == "h3" else ""}" href="#{html.escape(h["id"], quote=True)}">{html.escape(h["text"])}</a>' for h in record["headings"])
        variables = dict(LANG=lang, ROOT=prefix, TITLE=html.escape(record["title"], quote=True),
            DESCRIPTION=html.escape(record["description"], quote=True), VERSION=v, PAGE=page_id,
            LANGUAGE_NAME=html.escape(languages[lang]), GROUP=html.escape(record["group"]),
            BODY=record["body"], NAV=nav, PAGER='\n'.join(pager), TOC=toc,
            VERSIONS=version_options, LANGUAGES=language_options)
        rendered = re.sub(r"\{\{([A-Z_]+)\}\}", lambda m: variables[m[1]], template)
        if re.search(r"\{\{[A-Z_]+\}\}", rendered):
            raise ValueError("Unresolved template variable")
        result[root / record["url"]] = rendered
    landing = {"en": LandingTexts((root / "index.html").read_text(encoding="utf-8")).values}
    for lang in languages:
        translation = root / "_locales" / f"{lang}.json"
        if lang != "en" and translation.exists():
            values = json.loads(translation.read_text(encoding="utf-8"))
            if set(values) != set(landing["en"]):
                raise ValueError(f"Incomplete landing translation: {lang}")
            landing[lang] = values
    public = dict(defaultVersion=catalog["defaultVersion"], languages=languages, landing=landing,
        pages=[{k: val for k, val in p.items() if k != "body"} for p in records])
    result[root / "assets" / "catalog.js"] = '// Generated by scripts/build_public_docs.py. Do not edit.\nwindow.BazzaltDocs = ' + json.dumps(public, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c') + ';\n'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if checked-in generated files are stale")
    args = parser.parse_args()
    generated = outputs(DEFAULT_ROOT)
    stale = []
    for path, content in generated.items():
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")
    if args.check and stale:
        for path in stale:
            print(f"Stale: {path.relative_to(DEFAULT_ROOT)}")
        return 1
    print(f"Public docs {'checked' if args.check else 'built'}: {len(generated) - 1} pages.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
