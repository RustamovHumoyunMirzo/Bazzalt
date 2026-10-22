import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.controller import EditorController

class AntiAliasingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_camera_modes_dropdown_and_scene_roundtrip(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        resources=ResourceManager(Path(__file__).parents[1]/'assets');locale=LocalizationManager(resources)
        controller=EditorController.__new__(EditorController);controller.Window=SimpleNamespace(Localization=locale);controller._CommitComponent=Mock()
        editor=controller._ComponentEditor('camera','Camera','Anti Aliasing',2)
        try:
            self.assertEqual(editor.count(),8)
            self.assertEqual([editor.itemData(i) for i in range(8)],list(range(8)))
            self.assertEqual(editor.itemText(4),'MSAA 4×');editor.SetValue(7)
            controller._CommitComponent.assert_called_with('camera','Camera','Anti Aliasing',7)
            camera=runtime.CreateEntity('AA camera');self.assertTrue(runtime.AddComponent(camera,'Camera'))
            for mode in range(8):
                self.assertTrue(runtime.SetComponentProperty(camera,'Camera','Anti Aliasing',mode))
                snapshot=runtime.CaptureScene();self.assertTrue(runtime.RestoreScene(snapshot))
                self.assertEqual(runtime.EntityDetails(camera)['component_data']['Camera']['Anti Aliasing'],mode)
            for invalid in (-1,8,99,True,3.5):self.assertFalse(runtime.SetComponentProperty(camera,'Camera','Anti Aliasing',invalid))
            self.assertEqual(runtime.EntityDetails(camera)['component_data']['Camera']['Anti Aliasing'],7)
        finally:editor.deleteLater();runtime.Release()
