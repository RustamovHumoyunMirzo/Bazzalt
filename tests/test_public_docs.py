"""Validate the single-source static docs and automatic folder/filename discovery."""
from html.parser import HTMLParser
import json
from pathlib import Path
from unittest.mock import patch
import shutil
import tempfile
import unittest
from urllib.parse import unquote, urlsplit
from scripts.build_public_docs import DEFAULT_ROOT, Content, discover, outputs, read_article, flatten_pages, render_tree, tree


class Page(HTMLParser):
    def __init__(self, source):
        super().__init__();self.ids=set();self.references=[];self.resources=[];self.language=None;self.feed(source)

    def handle_starttag(self, tag, attrs):
        values=dict(attrs)
        if tag=='html': self.language=values.get('lang')
        if 'id' in values:
            if values['id'] in self.ids: raise ValueError('Duplicate HTML id')
            self.ids.add(values['id'])
        if 'href' in values: self.references.append(values['href'])
        if 'src' in values: self.resources.append(values['src'])
        if tag=='link' and values.get('rel')=='stylesheet': self.resources.append(values['href'])


class PublicDocsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.generated=outputs(DEFAULT_ROOT)

    def fixture(self, root):
        for name in ('index.html','templateExample.html'): shutil.copyfile(DEFAULT_ROOT/name,root/name)

    def add_page(self, root, relative, title):
        path=root/relative;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(f'<!doctype html><article class="doc"><h1>{title}</h1><p>Introduction.</p><h2 id="start">Start</h2><p>Details.</p></article>',encoding='utf-8')
        return path

    def test_refreshed_documents_are_current(self):
        for path,content in self.generated.items():
            with self.subTest(path=path): self.assertEqual(path.read_text(encoding='utf-8'),content)

    def test_discovery_has_host_independent_case_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            for name in ('zebra','Alpha','alphaExtra','Beta'):
                self.add_page(root,f'0.5.0/native/{name}/en.html',name)
            # Deliberately feed discovery opposite filesystem enumeration orders.
            paths=list((root/'0.5.0').rglob('*.html'))
            with patch.object(type(root),'rglob',return_value=iter(paths)):
                first=discover(root)[3]
            with patch.object(type(root),'rglob',return_value=iter(reversed(paths))):
                second=discover(root)[3]
            self.assertEqual(first,second)
            self.assertEqual([p['id'] for p in first],['native/Alpha','native/alphaExtra','native/Beta','native/zebra'])
            self.assertEqual([n['title'] for n in tree(first,{},'native')['children']],['Alpha','alphaExtra','Beta','zebra'])

    def test_all_links_and_anchors_resolve(self):
        pages={path:Page(source) for path,source in self.generated.items() if path.suffix=='.html'}
        for path,page in pages.items():
            if path.name!='index.html': self.assertEqual(page.language,path.stem)
            for reference in page.references+page.resources:
                with self.subTest(page=path,link=reference):
                    parsed=urlsplit(reference);self.assertFalse(parsed.scheme);self.assertFalse(parsed.netloc)
                    target=(path.parent/unquote(parsed.path)).resolve() if parsed.path else path
                    self.assertTrue(target.is_relative_to(DEFAULT_ROOT));self.assertTrue(target.is_file(),f'Missing target: {target}')
                    if parsed.fragment:
                        self.assertIn(target,pages);self.assertIn(unquote(parsed.fragment),pages[target].ids)

    def test_single_source_articles_are_preserved(self):
        articles={path:read_article(path.read_text(encoding='utf-8')) for path in self.generated if path.suffix=='.html' and path.name!='index.html'}
        self.assertGreaterEqual(len(articles),179)
        for path,article in articles.items():self.assertEqual(article,read_article(self.generated[path]))
        self.assertFalse((DEFAULT_ROOT/'_content').exists());self.assertFalse((DEFAULT_ROOT/'_api').exists());self.assertFalse((DEFAULT_ROOT/'catalog.json').exists())

    def test_add_page_language_and_version_without_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            self.add_page(root,'0.5.0/manual/en.html','Start here')
            self.add_page(root,'0.5.0/manual/new-guide/en.html','New guide')
            self.add_page(root,'0.5.0/manual/new-guide/ru.html','Новая страница')
            self.add_page(root,'0.6.0/manual/en.html','New version')
            options,versions,languages,records=discover(root)
            self.assertEqual(options,{})
            self.assertEqual(versions,['0.6.0','0.5.0']);self.assertEqual(languages,['en','ru'])
            built=outputs(root);translated=built[root/'0.5.0/manual/new-guide/ru.html']
            self.assertIn('lang="ru"',translated);self.assertIn('value="ru" selected',translated)
            self.assertIn('value="0.6.0"',translated)
            self.assertNotIn(root/'0.6.0/manual/ru.html',built)
            self.assertIn('href="0.6.0/manual/en.html"',built[root/'index.html'])
            self.assertNotIn('class="nav-link"',built[root/'0.5.0/manual/en.html'])
            self.assertIn('New guide',built[root/'assets/catalog.js'])

    def test_language_translation_is_one_optional_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            self.add_page(root,'0.5.0/manual/en.html','Start')
            self.add_page(root,'0.5.0/manual/uz.html','Boshlash')
            folder=root/'assets/languages';folder.mkdir(parents=True)
            (folder/'uz.json').write_text(json.dumps({'title':'Hujjatlar'}),encoding='utf-8')
            data=outputs(root)[root/'assets/catalog.js']
            catalog=json.loads(data.split('window.BazzaltDocs = ',1)[1].rstrip(';\n'))
            self.assertEqual(catalog['landing']['uz']['title'],'Hujjatlar')
            self.assertEqual(catalog['landing']['uz']['search'],catalog['landing']['en']['search'])

    def test_numeric_version_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            for version in ('0.9.0','0.10.0','0.10.0-beta.1'):self.add_page(root,version+'/manual/en.html','Start')
            self.assertEqual(discover(root)[1],['0.10.0','0.10.0-beta.1','0.9.0'])

    def test_folder_nesting_has_no_fixed_depth(self):
        records=[dict(id='manual/'+'/'.join(['x']*30),title='Deep page',url='deep/en.html')]
        nodes=tree(records,{})['children'];pages=list(flatten_pages(nodes))
        self.assertEqual(len(pages),1)
        markup=render_tree(nodes,records,'../../',records[0]['id'])
        self.assertEqual(markup.count('<details open>'),30)
        self.assertEqual(markup.count('aria-current="page"'),1)

    def test_reference_content_remains_present(self):
        for language,name,method in [('native','Scene','RaycastAll'),('native','Entity','AddComponent'),('native','MaterialBuilder','Build'),('lua','Mat4','Get'),('lua','PostProcessingStack','SetEffect')]:
            page=DEFAULT_ROOT/'0.5.0'/language/'reference'/name/'en.html'
            self.assertIn(f'id="{method}"',read_article(page.read_text(encoding='utf-8')))

    def test_home_has_language_not_version_or_categories(self):
        source=(DEFAULT_ROOT/'index.html').read_text(encoding='utf-8')
        self.assertIn('id="language"',source);self.assertNotIn('id="version"',source);self.assertNotIn('category',source.lower());self.assertNotIn('cdn.',source)

    def test_heading_ids_are_required_and_unique(self):
        for source in ('<h1>Title</h1><h2>Topic</h2>','<h1>Title</h1><h2 id="x">One</h2><h2 id="x">Two</h2>'):
            with self.assertRaises(ValueError):Content(source)

    def test_shared_navigation_and_nonempty_breadcrumbs(self):
        for path,source in self.generated.items():
            if path.suffix!='.html' or path.name=='index.html':continue
            self.assertNotIn('class="nav-link"',source)
            self.assertNotIn(' /  / ',source)
        data=self.generated[DEFAULT_ROOT/'assets/catalog.js']
        self.assertIn('"navigation":',data)


if __name__=='__main__': unittest.main()
