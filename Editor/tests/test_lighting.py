import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from types import SimpleNamespace
from PySide6.QtCore import QObject,Signal,Qt
from PySide6.QtWidgets import QApplication,QWidget
from Editor.gui.lighting import LightingDialog

class Runtime(QObject):
    ProjectChanging=Signal();SceneChanged=Signal()
    def __init__(self):super().__init__();self.Calls=[];self.Status=0;self.Error='';self.Apply=True
    def LightingCommand(self,command,*args):
        self.Calls.append(command)
        if command=='lighting_bake_info':return dict(status=self.Status,progress=.5,error=self.Error)
        if command=='apply_lighting_bake':return self.Apply

class LightingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def Make(self):
        parent=QWidget();parent.Runtime=Runtime();parent.Localization=SimpleNamespace(Translate=lambda key:key);parent.Controller=SimpleNamespace(_Mutate=lambda label,fn:fn(),SetDirty=lambda dirty:None,SelectedEntities=[1])
        return parent,LightingDialog(parent)
    def test_bake_apply_and_cancel(self):
        parent,dialog=self.Make();self.assertFalse(dialog.windowFlags() & Qt.WindowType.WindowMinimizeButtonHint)
        dialog.Begin();self.assertTrue(dialog.Capturing);self.assertFalse(dialog.Create.isEnabled());dialog.Poll();self.assertEqual(dialog.Progress.value(),500)
        parent.Runtime.Status=1;dialog.Poll();self.assertFalse(dialog.Capturing);self.assertFalse(dialog.Timer.isActive());self.assertIn('apply_lighting_bake',parent.Runtime.Calls);self.assertEqual(dialog.Status.text(),'lighting.complete')
        dialog.Begin();parent.Runtime.ProjectChanging.emit();self.assertFalse(dialog.Capturing);self.assertEqual(parent.Runtime.Calls[-1],'cancel_lighting_bake');dialog.reject();dialog.deleteLater();parent.deleteLater()
    def test_stale_bake_and_grid_authoring(self):
        parent,dialog=self.Make();dialog.CreateGrid();self.assertIn('create_light_probe_grid',parent.Runtime.Calls)
        dialog.Begin();parent.Runtime.Status=1;parent.Runtime.Apply=False;parent.Runtime.Error='Scene changed';dialog.Poll();self.assertEqual(dialog.Status.text(),'Scene changed')
        dialog.ClearBake();self.assertIn('clear_lighting_bake',parent.Runtime.Calls);dialog.Begin();dialog.reject();self.assertFalse(dialog.Timer.isActive());dialog.deleteLater();parent.deleteLater()

    def test_extended_authoring(self):
        parent,dialog=self.Make();dialog.CreateReflectionProbe();dialog.AddSelectedLightmaps()
        self.assertIn('create_reflection_probe',parent.Runtime.Calls);self.assertIn('add_lightmaps',parent.Runtime.Calls)
        dialog.deleteLater();parent.deleteLater()
