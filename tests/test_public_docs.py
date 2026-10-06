"""No browser/network dependencies: validate published docs and version/language builds."""
import copy
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from urllib.parse import unquote, urlsplit

from scripts.build_public_docs import DEFAULT_ROOT, Content, outputs, flatten_pages, render_tree
from scripts.public_api_docs import snapshot


class Page(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.ids = set()
        self.references = []
        self.resources = []
        self.language = None
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag == "html": self.language = values.get("lang")
        if "id" in values:
            if values["id"] in self.ids: raise ValueError("Duplicate HTML id")
            self.ids.add(values["id"])
        if "href" in values: self.references.append(values["href"])
        if "src" in values: self.resources.append(values["src"])
        if tag == "link" and values.get("rel") == "stylesheet": self.resources.append(values["href"])


class PublicDocsTests(unittest.TestCase):
    def test_generated_documents_are_current(self):
        for path, content in outputs(DEFAULT_ROOT).items():
            with self.subTest(path=path): self.assertEqual(path.read_text(encoding="utf-8"), content)

    def test_all_published_links_and_anchors_resolve(self):
        generated = outputs(DEFAULT_ROOT)
        pages = {path: Page(source) for path, source in generated.items() if path.suffix == ".html"}
        index = DEFAULT_ROOT / "index.html"
        pages[index] = Page(index.read_text(encoding="utf-8"))
        for path, page in pages.items():
            self.assertEqual(page.language, "en")
            for reference in page.references + page.resources:
                with self.subTest(page=path, link=reference):
                    parsed = urlsplit(reference)
                    self.assertFalse(parsed.scheme, "Docs resources/links must be local for offline use")
                    self.assertFalse(parsed.netloc)
                    target = (path.parent / unquote(parsed.path)).resolve() if parsed.path else path
                    self.assertTrue(target.is_relative_to(DEFAULT_ROOT), "Link escapes the public site")
                    self.assertTrue(target.is_file(), f"Missing link target: {target}")
                    if parsed.fragment:
                        self.assertIn(target, pages)
                        self.assertIn(unquote(parsed.fragment), pages[target].ids)

    def test_home_has_language_not_version_or_category_badges(self):
        source = (DEFAULT_ROOT / "index.html").read_text(encoding="utf-8")
        self.assertIn('id="language"', source)
        self.assertNotIn('id="version"', source)
        self.assertNotIn("category", source.lower())
        self.assertNotIn("cdn.", source)
        self.assertNotIn("job scheduling", source)

    def test_language_filenames_and_real_version(self):
        data = json.loads((DEFAULT_ROOT / "catalog.json").read_text(encoding="utf-8"))
        release = json.loads((DEFAULT_ROOT.parents[1] / "releases/versions.json").read_text(encoding="utf-8"))
        self.assertEqual(data["defaultVersion"], release["core"])
        for path in outputs(DEFAULT_ROOT):
            if path.suffix == ".html": self.assertEqual(path.name, "en.html")

    def test_second_version_and_partial_translation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("catalog.json", "index.html", "templateExample.html"):
                shutil.copyfile(DEFAULT_ROOT / name, root / name)
            shutil.copytree(DEFAULT_ROOT / "_content", root / "_content")
            shutil.copytree(DEFAULT_ROOT / "_api", root / "_api")
            catalog = json.loads((root / "catalog.json").read_text(encoding="utf-8"))
            old = catalog["versions"][0]
            newer = copy.deepcopy(old);newer["id"] = "0.6.0";newer["label"] = "0.6.0"
            catalog["versions"].append(newer);catalog["defaultVersion"] = "0.6.0"
            catalog["languages"]["fr"] = "Français"
            shutil.copytree(root / "_content/0.5.0", root / "_content/0.6.0")
            shutil.copyfile(root / "_api/0.5.0.json", root / "_api/0.6.0.json")
            (root / "_content/0.6.0/manual/fr.html").write_text('<h1>Introduction</h1><p>Guide.</p><h2 id="start">Commencer</h2>', encoding="utf-8")
            (root / "catalog.json").write_text(json.dumps(catalog), encoding="utf-8")
            built = outputs(root)
            translated = built[root / "0.6.0/manual/fr.html"]
            self.assertIn('lang="fr"', translated)
            self.assertIn('value="0.6.0" selected', translated)
            self.assertIn('value="fr" selected', translated)
            self.assertNotIn(root / "0.5.0/manual/fr.html", built)
            self.assertNotIn(root / "0.6.0/native/fr.html", built)
            self.assertIn('value="0.5.0" selected', built[root / "0.5.0/manual/en.html"])

    def test_source_requires_stable_unique_heading_ids(self):
        for source in ('<h1>Title</h1><h2>Topic</h2>', '<h1>Title</h1><h2 id="x">One</h2><h2 id="x">Two</h2>'):
            with self.assertRaises(ValueError): Content(source)

    def test_arbitrary_navigation_depth_and_active_ancestors(self):
        leaf={"id":"manual/deep", "title":"Deep page"}
        node=leaf
        for level in range(30): node={"title":f"Level {level}","children":[node]}
        pages=list(flatten_pages([node]))
        self.assertEqual([p["id"] for p in pages],["manual/deep"])
        available=[dict(p,url="0.5.0/manual/deep/en.html") for p in pages]
        tree=render_tree([node],available,"../../","manual/deep")
        self.assertEqual(tree.count('<details open>'),30)
        self.assertEqual(tree.count('aria-current="page"'),1)

    def test_api_snapshot_matches_public_headers_and_bindings(self):
        current=snapshot()
        saved=json.loads((DEFAULT_ROOT / "_api/0.5.0.json").read_text(encoding="utf-8"))
        self.assertEqual(saved,current)
        cpp={t['name']:t for t in current['cpp']};lua={t['name']:t for t in current['lua']}
        self.assertGreater(len(cpp),70);self.assertGreater(len(lua),60)
        for name, methods in [('Entity',['AddComponent','TryGetComponent','SetParent']),('Scene',['RaycastAll','TryGetInheritedComponent','RemoveSystem']),('MaterialBuilder',['SetShader','SetFloat','Build']),('Time',['Pause','Resume','SetMaximumDeltaTime']),('PostProcessingStack',['AddEffect','RemoveEffect','Clear'])]:
            self.assertTrue(set(methods)<= {m['name'] for m in cpp[name]['members']})
        self.assertIn('Get', {m['name'] for m in lua['Mat4']['members']})
        self.assertNotIn('Data', {m['name'] for m in lua['Mat4']['members']})
        self.assertNotIn('Normalize', {m['name'] for m in lua['Vec3']['members']})
        self.assertIn('SetEffect', {m['name'] for m in lua['PostProcessingStack']['members']})
        for language in ('cpp','lua'):
            for type_ in current[language]:
                self.assertTrue(type_['description'])
                for member in type_['members']:self.assertTrue(member['description'])


if __name__ == "__main__": unittest.main()
