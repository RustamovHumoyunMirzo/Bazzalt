import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from pathlib import Path
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.console import ConsolePanel,ConsoleLevel

class ScriptingConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_shared_native_messages_clear_and_filtered_removal(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        resources=ResourceManager(Path(__file__).parents[1]/'assets');panel=ConsolePanel(LocalizationManager(resources),resources)
        try:
            runtime.ConsoleClear();panel.AddMessage('before binding');panel.BindRuntime(runtime)
            self.assertEqual(runtime.ConsoleSnapshot(0)['messages'][0]['text'],'before binding')
            panel.AddMessage('日本語 hello',ConsoleLevel.Info,False,'Lua')
            runtime.ConsoleAdd('native warning',2,'Physics',True);panel.RefreshNative()
            self.assertEqual(len(panel.GetMessages()),3);self.assertEqual(panel.View.count(),3)
            messages=runtime.ConsoleSnapshot(0)['messages'];self.assertFalse(messages[1]['icon'])
            self.assertEqual(messages[1]['text'],'日本語 hello')
            panel.SetSourceFilter('Physics');panel.ClearFiltered()
            self.assertEqual(len(panel.GetMessages()),2)
            panel.SetSourceFilter('');panel.View.item(1).setSelected(True);panel.RemoveSelected()
            self.assertEqual(len(panel.GetMessages()),1)
            runtime.ConsoleRemove([panel.GetMessages()[0].Id]);panel.RefreshNative();self.assertEqual(panel.View.count(),0)
            panel.AddMessage('last');panel.Clear();self.assertEqual(runtime.ConsoleSnapshot(0)['messages'],[])
            runtime.ConsoleAdd(b'\xff',1,'Binary',False);panel.RefreshNative()
            self.assertEqual(panel.GetMessages()[0].Text,'\ufffd')
            panel.Clear();panel.AddMessage('first');panel.AddMessage('second');panel.AddMessage('third')
            panel.View.item(1).setSelected(True)
            runtime.ConsoleRemove([panel.GetMessages()[0].Id]);panel.RefreshNative()
            self.assertEqual(len(panel.View.selectedItems()),1);self.assertIn('second',panel.View.selectedItems()[0].text())
        finally:panel.deleteLater();runtime.Release()
