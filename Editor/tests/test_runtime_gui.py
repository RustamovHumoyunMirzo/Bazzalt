import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import Mock,patch
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.gui.controller import EditorController
from Editor.gui.panels.hierarchy import HierarchyPanel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from PySide6.QtCore import QPoint
from PySide6.QtWidgets import QMenu

class RuntimeGuiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Runtime=RuntimeService()
        if not self.Runtime.IsAvailable():self.skipTest("Native runtime is unavailable")
    def tearDown(self):self.Runtime.Release()
    def test_gui_components_fields_validation_and_camera_independent_output(self):
        runtime=self.Runtime;frame=runtime.CreateEntity("HUD");button=runtime.CreateEntity("Button")
        self.assertIn("Frame",runtime.ComponentTypes());self.assertIn("GuiTextInput",runtime.ComponentTypes())
        self.assertTrue(runtime.AddComponent(frame,"Frame"));self.assertTrue(runtime.AddComponent(button,"RectTransform"));self.assertTrue(runtime.AddComponent(button,"Rectangle"));self.assertTrue(runtime.AddComponent(button,"GuiButton"))
        self.assertFalse(runtime.HasActiveCamera());self.assertTrue(runtime.HasGameOutput())
        self.assertTrue(runtime.SetComponentProperty(button,"RectTransform","Size",[200,32]))
        self.assertFalse(runtime.SetComponentProperty(frame,"Frame","ReferenceSize",[-1,720]))
        row=next(entity for entity in runtime.Entities() if entity["uuid"]==button)
        self.assertEqual(list(row["component_data"]["RectTransform"]["Size"]),[200,32]);self.assertTrue(row["component_enabled"]["Rectangle"])
        self.assertTrue(runtime.SetComponentEnabled(frame,"Frame",False));self.assertFalse(runtime.HasGameOutput())
        self.assertTrue(runtime.RemoveComponent(button,"GuiButton"))
        row=next(entity for entity in runtime.Entities() if entity["uuid"]==button);self.assertNotIn("GuiButton",row["components"])

    def test_hierarchy_gui_submenu_emits_creation_type(self):
        panel=HierarchyPanel(LocalizationManager(ResourceManager()),install_shortcuts=False)
        requested=[];panel.CreateTypedRequested.connect(lambda kind,parent:requested.append((kind,parent)))
        test=self
        class InspectMenu(QMenu):
            def exec(menu,position):
                actions=menu.actions();create=actions[0].menu();create_actions=create.actions()
                gui=next(action.menu() for action in create_actions if action.text()=="GUI")
                gui_actions=gui.actions();test.assertEqual(len(gui_actions),9)
                next(action for action in gui_actions if action.text()=="Button").trigger()
        with patch("Editor.gui.panels.hierarchy.QMenu",InspectMenu):panel._ShowContextMenu(QPoint(-1,-1))
        self.assertEqual(requested,[("GUI:button",None)]);panel.deleteLater()

    def test_gui_authoring_reuses_frame_and_is_single_history_operation(self):
        locale=LocalizationManager(ResourceManager());history=Mock();selected=[]
        controller=SimpleNamespace(Runtime=self.Runtime,Window=SimpleNamespace(Localization=locale),History=history,
            _SceneForParent=lambda parent:"",SetDirty=Mock(),SelectEntity=lambda entity,**kwargs:selected.append(entity))
        EditorController._CreateGuiEntity(controller,"button",None)
        first=self.Runtime.EntityDetails(selected[-1]);frame=first["parent"]
        self.assertEqual(set(first["components"])&{"RectTransform","Rectangle","GuiText","GuiButton"},{"RectTransform","Rectangle","GuiText","GuiButton"})
        self.assertIn("Frame",self.Runtime.EntityDetails(frame)["components"])
        EditorController._CreateGuiEntity(controller,"text_input",selected[-1])
        self.assertEqual(sum("Frame" in row["components"] for row in self.Runtime.Entities()),1)
        self.assertEqual(history.Begin.call_count,2);self.assertEqual(history.Commit.call_count,2);history.Cancel.assert_not_called()
        EditorController._CreateGuiEntity(controller,"spatial",None)
        self.assertEqual(self.Runtime.EntityDetails(selected[-1])["component_data"]["Frame"]["Mode"],2)

    @unittest.skipUnless(os.name=="nt","Windows module deployment regression")
    def test_missing_sibling_gui_module_reports_error_instead_of_searching_cwd(self):
        native=sys.modules.get("_bazzalt_runtime") or sys.modules.get("Editor._bazzalt_runtime")
        if native is None:self.skipTest("Cannot locate the native extension")
        source=Path(native.__file__).parent
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/"build",prefix="gui-module-") as directory:
            folder=Path(directory)
            for name in (Path(native.__file__).name,"Bazzalt.dll","bazzalt_lua.dll","bshad.dll"):
                if (source/name).is_file():shutil.copy2(source/name,folder/name)
            code="""import importlib.util,os,sys
handle=os.add_dll_directory(sys.argv[1])
spec=importlib.util.spec_from_file_location('_bazzalt_runtime',sys.argv[2])
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
try:module.EditorHost()
except RuntimeError as error:
    assert 'bazzalt_gui.dll' in str(error),str(error)
    print(str(error))
else:raise AssertionError('Loaded GUI from outside the core runtime directory')
"""
            result=subprocess.run([sys.executable,"-c",code,str(folder),str(folder/Path(native.__file__).name)],cwd=source,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn("bazzalt_gui.dll",result.stdout)

if __name__=="__main__":unittest.main()
