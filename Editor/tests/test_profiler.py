import os
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
import unittest
from types import SimpleNamespace
from PySide6.QtWidgets import QApplication, QWidget
from Editor.gui.profiler import ProfilerDialog


class ProfilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_capture_aggregation_and_close(self):
        calls = []
        class Runtime:
            def ProfileCommand(self, command, *args):
                calls.append(command)
                if command == 'profile_samples':
                    return [dict(name='Work', duration_us=1000), dict(name='Work', duration_us=3000)]
        parent = QWidget()
        parent.Runtime = Runtime()
        parent.Localization = SimpleNamespace(Translate=lambda key: key)
        dialog = ProfilerDialog(parent)
        dialog.Begin()
        self.assertTrue(dialog.Capturing)
        dialog.reject()
        self.assertFalse(dialog.Capturing)
        item = dialog.Table.topLevelItem(0)
        self.assertEqual([item.text(i) for i in range(4)], ['Work', '2', '4.000', '3.000'])
        self.assertEqual(calls, ['begin_profile_capture', 'end_profile_capture', 'profile_samples'])
        dialog.deleteLater()
        parent.deleteLater()

    def test_missing_runtime_reports_error(self):
        parent = QWidget()
        def fail(*args):
            raise RuntimeError('unavailable')
        parent.Runtime = SimpleNamespace(ProfileCommand=fail)
        parent.Localization = SimpleNamespace(Translate=lambda key: key)
        dialog = ProfilerDialog(parent)
        dialog.Begin()
        self.assertFalse(dialog.Capturing)
        self.assertEqual(dialog.Status.text(), 'unavailable')
        dialog.deleteLater()
        parent.deleteLater()
