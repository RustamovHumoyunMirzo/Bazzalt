"""Compiler helper imports must work without the editor's Qt dependencies."""
import subprocess
import sys
import unittest
from pathlib import Path


class HeadlessImportTests(unittest.TestCase):
    def test_shared_helpers_do_not_import_qt(self):
        root = Path(__file__).resolve().parents[2]
        program = '''
import importlib.abc
import sys

class NoQt(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('PySide6', 'shiboken6'):
            raise ModuleNotFoundError('Qt is unavailable on the compiler runner')

sys.meta_path.insert(0, NoQt())
from Editor.scripting import ScriptCompiler, ScriptAttachments
from Editor.subsystems import Registrations
from Compiler.build import CompileScripts
from Compiler.content import FilterCooked
from Editor.materials import MaterialCompiler
assert Registrations({}) == []
import unittest
sys.path.insert(0, 'Compiler/tests')
suite = unittest.defaultTestLoader.loadTestsFromNames(['test_compiler', 'test_content'])
assert unittest.TextTestRunner().run(suite).wasSuccessful()
assert 'Editor.gui' not in sys.modules
assert not any(name.startswith('PySide6') for name in sys.modules)
'''
        result = subprocess.run([sys.executable, '-c', program], cwd=root,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
