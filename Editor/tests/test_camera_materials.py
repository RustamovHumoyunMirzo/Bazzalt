import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from PySide6.QtWidgets import QApplication,QPushButton,QCheckBox
from Editor.gui.widgets.post_materials import CameraMaterialsInput,ZERO
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager

class CameraMaterialsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_order_enable_remove_and_picker_changes_emit_complete_list(self):
        pickers=[]
        def picker(asset,commit):
            field=QPushButton(asset);pickers.append(commit);return field
        editor=CameraMaterialsInput([{'asset':'a','enabled':True},{'asset':'b','enabled':False}],LocalizationManager(ResourceManager()),picker)
        changes=[];editor.ValueChanged.connect(changes.append)
        editor._Move(1,-1);self.assertEqual([v['asset'] for v in changes[-1]],['b','a'])
        pickers[-2]('c');self.assertEqual(changes[-1][0]['asset'],'c')
        editor._Field(0,'enabled',True);self.assertTrue(changes[-1][0]['enabled'])
        editor._Remove(1);self.assertEqual(len(changes[-1]),1)
        editor._Add();self.assertEqual(changes[-1][1],{'asset':ZERO,'enabled':True})
        changes[-1][0]['asset']='external mutation';self.assertEqual(editor.GetValue()[0]['asset'],'c')
        count=len(changes);editor.SetValue([]);self.assertEqual(len(changes),count)
        editor.deleteLater();self.App.processEvents()
    def test_pass_limit_does_not_emit_an_invalid_list(self):
        editor=CameraMaterialsInput([{'asset':ZERO,'enabled':True} for _ in range(32)],LocalizationManager(ResourceManager()),lambda a,c:QPushButton())
        changes=[];editor.ValueChanged.connect(changes.append);editor._Add()
        self.assertEqual(len(editor.GetValue()),32);self.assertEqual(changes,[])
        editor.deleteLater();self.App.processEvents()

    def test_native_inspector_stack_edit_undo_and_redo_preserve_selection(self):
        from PySide6.QtCore import QEvent
        from Editor.gui.application import Editor
        from Editor.theme import ThemeManager
        window=Editor(ThemeManager(self.App))
        try:
            if not window.Runtime.IsAvailable():self.skipTest("native editor bridge is not built")
            entity=window.Runtime.CreateEntity("Material stack camera")
            self.assertTrue(window.Runtime.AddComponent(entity,"Camera"))
            controller=window.Controller;controller.SelectEntity(entity)
            field=window.Properties._sections["runtime.Camera"]._fields["Post Process Materials"]
            self.assertIsInstance(field,CameraMaterialsInput)
            field._Add()
            values=lambda:window.Runtime.EntityDetails(entity)["component_data"]["Camera"]["Post Process Materials"]
            self.assertEqual(values(),[{'asset':ZERO,'enabled':True}])
            self.assertEqual(controller.SelectedEntity,entity)
            controller.Undo();self.assertEqual(values(),[])
            self.assertEqual(controller.SelectedEntity,entity)
            controller.Redo();self.assertEqual(values(),[{'asset':ZERO,'enabled':True}])
            current=window.Properties._sections["runtime.Camera"]._fields["Post Process Materials"]
            self.assertEqual(current.GetValue(),values())
        finally:
            window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
