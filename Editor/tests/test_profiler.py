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

    def test_timeline_frame_selection_and_window_controls(self):
        from PySide6.QtCore import Qt
        samples=[dict(name='Editor.Frame',start_us=100,duration_us=1000,thread=1),
                 dict(name='Main',start_us=200,duration_us=100,thread=1),
                 dict(name='Worker',start_us=250,duration_us=200,thread=2),
                 dict(name='Later',start_us=3000,duration_us=500,thread=1)]
        class Runtime:
            def ProfileCommand(self,command,*args):
                return samples if command=='profile_samples' else None
            def Statistics(self):return {'gpu_frame_ms':-1,'objects':12,'primitive_cache_bytes':4096}
        parent=QWidget();parent.Runtime=Runtime();parent.Localization=SimpleNamespace(Translate=lambda key:key)
        dialog=ProfilerDialog(parent)
        self.assertFalse(dialog.windowFlags() & Qt.WindowType.WindowMinimizeButtonHint)
        dialog.Begin();dialog.Poll();dialog.End();self.assertFalse(dialog.Timer.isActive())
        self.assertEqual(dialog.Graph.Values,[1.0]);dialog.SelectFrame(0)
        self.assertEqual({s['name'] for s in dialog.Timeline.Samples},{'Editor.Frame','Main','Worker'})
        dialog.All.click();self.assertEqual(len(dialog.Timeline.Samples),4)
        dialog.Metric.setCurrentIndex(dialog.Metric.findData('gpu_frame_ms'));self.assertEqual(dialog.SystemGraph.Values,[])
        dialog.Metric.setCurrentIndex(dialog.Metric.findData('objects'));self.assertEqual(dialog.SystemGraph.Values,[12])
        dialog.resize(900,600);dialog.show();self.App.processEvents();self.assertFalse(dialog.grab().isNull())
        for _ in range(260):dialog.Poll()
        self.assertEqual(len(dialog.Snapshots),240)
        dialog.reject();dialog.deleteLater();parent.deleteLater()
