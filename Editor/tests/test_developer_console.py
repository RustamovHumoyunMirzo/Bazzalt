import os,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.console import ConsolePanel

class DeveloperConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_native_command_history_completion_and_clear(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        resources=ResourceManager(Path(__file__).resolve().parents[1]/'assets');panel=ConsolePanel(LocalizationManager(resources),resources)
        try:
            panel.BindRuntime(runtime);self.assertTrue(panel.Command.isEnabled())
            panel.Command.setText('app.target_fps 90');panel._ExecuteCommand()
            self.assertEqual(panel.Command.text(),'');self.assertEqual(runtime.DeveloperHistory()[-1],'app.target_fps 90')
            self.assertIn('app.target_fps',runtime.DeveloperComplete('app.target'))
            result=runtime.DeveloperExecute('app.target_fps');self.assertTrue(result['success']);self.assertIn('90',result['output'])
            panel.RefreshNative();self.assertTrue(any('app.target_fps' in m.Text for m in panel.GetMessages()))
            panel.Command.setText('draft');QTest.keyClick(panel.Command,Qt.Key.Key_Up);self.assertEqual(panel.Command.text(),'app.target_fps')
            QTest.keyClick(panel.Command,Qt.Key.Key_Down);self.assertEqual(panel.Command.text(),'draft')
            panel.Command.Index=100;QTest.keyClick(panel.Command,Qt.Key.Key_Up);self.assertEqual(panel.Command.text(),runtime.DeveloperHistory()[-1])
            panel.Command.setText('app.target_f');QTest.keyClick(panel.Command,Qt.Key.Key_Tab);self.assertEqual(panel.Command.text(),'app.target_fps ')
            runtime.DeveloperExecute('clear');panel.RefreshNative();self.assertEqual(panel.View.count(),0)
            self.assertFalse(runtime.DeveloperExecute('app.target_fps 1001')['success'])
        finally:panel.deleteLater();runtime.Release()
