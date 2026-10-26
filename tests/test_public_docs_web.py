"""Optional headless browser smoke tests using the existing Qt WebEngine install.

Run: python -m unittest discover -s tests -p test_public_docs_web.py -v
No native BAZZALT module, HTTP server, or browser download is needed.
"""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu")
from pathlib import Path
import json
import unittest

try:
    from PySide6.QtCore import QEventLoop, QTimer, QUrl
    from PySide6.QtWidgets import QApplication
    from PySide6.QtWebEngineCore import QWebEnginePage, QWebEngineProfile, QWebEngineUrlRequestInterceptor
    from PySide6.QtWebEngineWidgets import QWebEngineView
    AVAILABLE = True
except ImportError:
    AVAILABLE = False

ROOT = Path(__file__).resolve().parents[1] / "docs/public"

if AVAILABLE:
    class Requests(QWebEngineUrlRequestInterceptor):
        def __init__(self,parent):super().__init__(parent);self.urls=[]
        def interceptRequest(self,info):self.urls.append(info.requestUrl().toString())


@unittest.skipUnless(AVAILABLE, "Optional Qt WebEngine is not installed")
class PublicDocsWebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.profile = QWebEngineProfile()  # Off-the-record: no persistent browser data.
        self.requests=Requests(self.profile);self.profile.setUrlRequestInterceptor(self.requests)
        self.view = QWebEngineView()
        self.page = QWebEnginePage(self.profile, self.view)
        self.view.setPage(self.page)
        self.view.resize(1280, 900)
        self.view.show()

    def tearDown(self):
        self.view.close()
        self.page.deleteLater()
        self.view.deleteLater()
        self.app.processEvents()
        self.profile.deleteLater()
        self.app.processEvents()

    def await_callback(self, start):
        loop = QEventLoop();values = []
        def done(value):
            values.append(value);loop.quit()
        timer = QTimer();timer.setSingleShot(True);timer.timeout.connect(loop.quit)
        timer.start(15000);start(done)
        if not values: loop.exec()
        timer.stop()
        self.assertTrue(values, "Browser callback timed out")
        return values[0]

    def load(self, path):
        def start(done):
            def finished(ok):
                self.view.loadFinished.disconnect(finished);done(ok)
            self.view.loadFinished.connect(finished)
            self.view.load(QUrl.fromLocalFile(str(ROOT / path)))
        self.assertTrue(self.await_callback(start))
        if path != 'index.html':
            self.wait_js("document.documentElement.dataset.navigationReady === 'true'")

    def js(self, source):
        return self.await_callback(lambda done: self.page.runJavaScript(source, done))

    def wait_js(self, expression):
        # Poll observable completion rather than assuming async scripts load
        # within one fixed delay on every CI machine.
        for _ in range(150):
            if self.js(expression):return
            self.await_callback(lambda done:QTimer.singleShot(20,lambda:done(True)))
        self.fail('Browser condition timed out: '+expression)

    def wait_search(self):
        self.wait_js("!document.getElementById('search-status').textContent.startsWith('Loading') && document.getElementById('search-status').textContent.length > 0")

    def test_file_mode_search_theme_and_navigation(self):
        self.load("index.html")
        self.assertEqual(self.js("document.querySelectorAll('#version').length"), 0)
        self.assertEqual(self.js("document.getElementById('language').value"), "en")
        self.assertEqual(self.js("document.querySelector('[data-doc-link=manual]').getAttribute('href').endsWith('/'+window.BazzaltDocs.defaultVersion+'/manual/en.html')"), True)
        original = self.js("document.documentElement.classList.contains('dark')")
        self.js("document.querySelector('.theme-btn').click()")
        self.assertNotEqual(self.js("document.documentElement.classList.contains('dark')"), original)
        self.js("document.querySelector('[data-search-open]').click();var input=document.getElementById('search-input');input.value='roughness';input.dispatchEvent(new Event('input'))")
        self.assertTrue(self.js("document.getElementById('search-dialog').open"))
        self.wait_search()
        self.assertGreater(self.js("document.querySelectorAll('#search-results a').length"), 0)
        self.assertTrue(self.js("[...document.querySelectorAll('#search-results a')].some(a=>a.href.includes('/native/materials/en.html'))"))
        self.js("document.querySelector('[data-search-close]').click()")
        self.assertFalse(self.js("document.getElementById('search-dialog').open"))
        self.load("0.5.0/native/materials/en.html")
        self.assertEqual(self.js("document.querySelector('.nav-link[aria-current=page]').textContent"), "Runtime materials")
        self.assertGreater(self.js("document.querySelectorAll('.toc-link').length"), 3)
        self.assertGreater(self.js("document.querySelectorAll('.copy-code').length"), 0)
        self.assertEqual(self.js("document.getElementById('version').value"), "0.5.0")
        self.js("document.querySelector('.toc-link[href=\"#assignment\"]').click()")
        self.assertEqual(self.js("location.hash"), "#assignment")
        self.assertTrue(self.js("document.documentElement.scrollWidth<=window.innerWidth"))

    def test_search_is_lazy_release_scoped_and_reused(self):
        self.load('index.html')
        self.assertFalse(any('/assets/navigation/' in u or '/assets/search/' in u for u in self.requests.urls))
        self.load('0.6.0/native/audio/en.html')
        self.assertFalse(any('/assets/search/' in u for u in self.requests.urls))
        self.assertEqual(sum('/assets/navigation/' in u for u in self.requests.urls),1)
        self.js("document.querySelector('[data-search-open]').click()")
        self.wait_search()
        self.assertLessEqual(self.js("document.querySelectorAll('#search-results a').length"),60)
        self.assertEqual(sum('/assets/search/' in u for u in self.requests.urls),1)
        self.assertTrue(any('/assets/search/0.6.0/en.js' in u for u in self.requests.urls))
        self.js("document.querySelector('[data-search-close]').click();document.querySelector('[data-search-open]').click()")
        self.wait_search()
        self.assertEqual(sum('/assets/search/' in u for u in self.requests.urls),1)

    def test_failed_search_bundle_can_be_retried(self):
        self.load('index.html')
        self.js("var v=BazzaltDocs.defaultVersion,l=BazzaltDocs.defaultLanguage;var source=BazzaltDocs.search[v][l];BazzaltDocs.search[v][l]='assets/not-present.js';document.querySelector('[data-search-open]').click()")
        self.wait_js("document.getElementById('search-status').textContent.includes('Could not load')")
        self.js("document.querySelector('[data-search-close]').click();BazzaltDocs.search[v][l]=source;document.querySelector('[data-search-open]').click()")
        self.wait_search()
        self.assertGreater(self.js("document.querySelectorAll('#search-results a').length"),0)

    def test_mobile_navigation_and_empty_search(self):
        self.view.resize(390, 844)
        self.load("0.5.0/lua/en.html")
        self.assertTrue(self.js("document.documentElement.scrollWidth<=window.innerWidth"))
        self.assertEqual(self.js("getComputedStyle(document.querySelector('.nav-toggle')).display"), "block")
        self.js("document.querySelector('.nav-toggle').click()")
        self.assertEqual(self.js("document.querySelector('.nav-toggle').getAttribute('aria-expanded')"), "true")
        self.assertNotEqual(self.js("getComputedStyle(document.getElementById('sidebar')).display"), "none")
        self.js("document.querySelector('[data-search-open]').click();var input=document.getElementById('search-input');input.value='zzzzmissing';input.dispatchEvent(new Event('input'))")
        self.wait_js("document.getElementById('search-status').textContent.includes('No matching')")
        self.assertEqual(self.js("document.querySelectorAll('#search-results a').length"), 0)
        self.assertIn("No matching", self.js("document.getElementById('search-status').textContent"))

    def test_identity_and_selectors_come_from_url_and_shared_catalog(self):
        self.load("0.6.0/native/audio/en.html")
        self.assertEqual(self.js("document.body.hasAttribute('data-version')"),False)
        self.assertEqual(self.js("document.body.hasAttribute('data-page')"),False)
        self.assertEqual(self.js("document.documentElement.lang"),'en')
        self.assertEqual(self.js("document.getElementById('version').value"),'0.6.0')
        versions=[path for path in ROOT.iterdir() if path.is_dir() and (path/'manual/en.html').is_file()]
        self.assertEqual(self.js("document.querySelectorAll('#version option').length"),len(versions))
        self.assertIn('0.6.0',self.js("document.querySelector('[data-doc-footer]').textContent"))
        self.assertEqual(self.js("document.querySelector('.nav-link[aria-current=page]').textContent"),'C++ audio')
        self.assertTrue(self.js("document.querySelector('.pager a').getAttribute('href').indexOf('0.6.0')===-1"))

    def settle(self):
        self.await_callback(lambda done: QTimer.singleShot(120, lambda: done(True)))

    def test_last_short_contents_section_and_explicit_click(self):
        self.load("0.5.0/manual/en.html")
        last=self.js("[...document.querySelectorAll('.toc-link')].at(-1).hash.slice(1)")
        self.js("window.scrollTo(0,document.documentElement.scrollHeight)")
        self.settle()
        self.assertEqual(self.js("document.querySelector('.toc-link[aria-current=location]').hash.slice(1)"),last)
        self.js("document.querySelectorAll('.toc-link')[1].click()")
        self.settle()
        clicked=self.js("document.querySelectorAll('.toc-link')[1].hash.slice(1)")
        self.assertEqual(self.js("document.querySelector('.toc-link[aria-current=location]').hash.slice(1)"),clicked)
        self.js("[...document.querySelectorAll('.toc-link')].at(-1).click()")
        self.settle()
        self.assertEqual(self.js("document.querySelector('.toc-link[aria-current=location]').hash.slice(1)"),last)
        self.js("window.dispatchEvent(new WheelEvent('wheel'));window.scrollTo(0,0)")
        self.settle()
        first=self.js("document.querySelector('.toc-link').hash.slice(1)")
        self.assertEqual(self.js("document.querySelector('.toc-link[aria-current=location]').hash.slice(1)"),first)

    def test_shared_sidebar_highlighting_and_stable_asset_loading(self):
        self.load("0.5.0/bshader/cookbook/en.html")
        self.assertGreater(self.js("document.querySelectorAll('#sidebar .nav-link').length"),170)
        self.assertEqual(self.js("document.querySelector('.nav-link[aria-current=page]').textContent"),"Bshader examples")
        self.assertEqual(self.js("document.querySelector('.code-language').textContent"),"Bshader")
        self.assertGreater(self.js("document.querySelectorAll('.token-keyword').length"),0)
        self.assertTrue(self.js("document.querySelector('pre code').textContent.startsWith('shader SolidColor {')"))
        self.assertNotIn('/ /',self.js("document.querySelector('.breadcrumbs').textContent"))
        self.settle();before=len(self.requests.urls)
        self.await_callback(lambda done:QTimer.singleShot(650,lambda:done(True)))
        self.assertEqual(len(self.requests.urls),before,"Idle pages must not reload assets")
        for resource in ('catalog.js','docs.js','docs.css','theme.js'):
            self.assertEqual(sum(url.endswith('/assets/'+resource) for url in self.requests.urls),1,resource)
        self.assertEqual(self.js("document.querySelectorAll('.code-toolbar').length"),6)
        self.js("eval("+json.dumps((ROOT/'assets/docs.js').read_text(encoding='utf-8'))+")")
        self.assertEqual(self.js("document.querySelectorAll('.code-toolbar').length"),6,"Repeated initialization must be harmless")


if __name__ == "__main__": unittest.main()
