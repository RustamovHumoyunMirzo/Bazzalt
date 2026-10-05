import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from PySide6.QtCore import QEvent, Qt
from PySide6.QtGui import QKeyEvent
from PySide6.QtWidgets import QApplication, QLineEdit, QMainWindow, QWidget
from PySide6.QtTest import QTest
from Editor.gui.object_tools import ObjectTools, SelectionRoots, ViewRotation
from Editor.gui.widgets.editor_toolbar import EditorToolbar
from Editor.gui.widgets.menu_bar import EditorMenuBar
from Editor.gui.panels.viewport import NativeRenderSurface
from Editor.gui.gizmos import Vec3, Ray
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import ThemeManager

class ObjectToolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])

    def test_selection_roots_and_camera_rotation(self):
        self.assertEqual(SelectionRoots(["child","parent","other"],{"child":"parent"}),["parent","other"])
        self.assertEqual(ViewRotation((0,0,0),(0,0,-1)),(0,0,0,1))
        self.assertIsNone(ViewRotation((0,0,0),(0,0,0)))
        for target in ((1,0,0),(0,1,0),(0,-1,0),(0,0,1)):
            self.assertAlmostEqual(sum(v*v for v in ViewRotation((0,0,0),target)),1)

    def test_shared_shortcuts_and_typing_guard(self):
        window=QMainWindow();themes=ThemeManager(self.App);locale=LocalizationManager(ResourceManager())
        window.Toolbar=EditorToolbar(locale,themes,window);window.MenuBar=EditorMenuBar(themes,locale,window)
        window.Controller=Mock();window.Runtime=Mock()
        window.Scene=SimpleNamespace(Surface=SimpleNamespace(_navigating=False,_gizmo_drag=None))
        window.Output=SimpleNamespace(Surface=QWidget(),_no_camera=QWidget())
        tools=ObjectTools(window)
        try:
            self.assertEqual([a.shortcut().toString() for a in window.Toolbar.ModeActions.values()],["1","2","3","4"])
            self.assertEqual(window.MenuBar.ObjectModeMenu.actions(),list(window.Toolbar.ModeActions.values()))
            field=QLineEdit(window)
            for key in (Qt.Key.Key_1,Qt.Key.Key_2,Qt.Key.Key_3,Qt.Key.Key_4):
                event=QKeyEvent(QEvent.Type.ShortcutOverride,key,Qt.KeyboardModifier.NoModifier)
                with patch.object(QApplication,"focusWidget",return_value=field):
                    self.assertTrue(tools.eventFilter(field,event));self.assertTrue(event.isAccepted())
            window.setCentralWidget(field);window.show();window.activateWindow();field.setFocus();self.App.processEvents()
            previous=window.Toolbar.GetGizmoMode()
            QTest.keyClicks(field,"1234");self.assertEqual(field.text(),"1234")
            from Editor.gui.gizmos import GizmoMode
            self.assertEqual(window.Toolbar.GetGizmoMode(),previous)
            field.clearFocus();window.setFocus();self.App.processEvents()
            for number,mode in enumerate(GizmoMode,1):
                QTest.keyClick(window,getattr(Qt.Key,"Key_"+str(number)));self.App.processEvents()
                self.assertEqual(window.Toolbar.GetGizmoMode(),mode)
            for navigation,drag,focus in ((True,None,window),(False,object(),window),(False,None,window.Output.Surface)):
                window.Scene.Surface._navigating=navigation;window.Scene.Surface._gizmo_drag=drag
                window.Runtime.IsPlaying.return_value=True;window.Runtime.IsPaused.return_value=False
                with patch.object(QApplication,"focusWidget",return_value=focus):
                    self.assertTrue(tools.eventFilter(window,QKeyEvent(QEvent.Type.ShortcutOverride,Qt.Key.Key_2,Qt.KeyboardModifier.NoModifier)))
        finally:tools.Close();window.close();window.deleteLater()

    def test_placement_surface_and_grid(self):
        runtime=Mock();surface=NativeRenderSurface(runtime,True);surface._has_scene_pointer=True
        surface._Ray=lambda _:Ray(Vec3(0,5,0),Vec3(0,-1,0))
        try:
            runtime.RaycastEditor.return_value=(2,3,4)
            self.assertEqual(surface.PlacementPosition(1,["parent"]),(2,3,4))
            self.assertEqual(runtime.RaycastEditor.call_args.args[2],["parent"])
            runtime.RaycastEditor.return_value=None
            self.assertEqual(surface.PlacementPosition(1),(0,0,0))
            self.assertIsNone(surface.PlacementPosition(0))
            surface._has_scene_pointer=False;self.assertIsNone(surface.PlacementPosition(1))
        finally:surface.close()

    def test_group_placement_moves_root_once_and_commits_undo(self):
        controller=Mock();controller.SelectedEntities=["parent","child","other"]
        runtime=Mock();runtime.EditorParents={"child":"parent"};runtime.IsEditorSelectable.return_value=True
        details={k:{"position":p,"rotation":(0,0,0,1),"scale":(1,1,1),"scene_uuid":"scene"} for k,p in (("parent",(2,0,0)),("other",(4,0,0)))}
        runtime.EntityDetails.side_effect=details.get;runtime.SetTransform.return_value=True
        window=SimpleNamespace(Scene=SimpleNamespace(Surface=Mock()),Localization=Mock())
        tools=SimpleNamespace(Controller=controller,Runtime=runtime,Window=window)
        ObjectTools.Execute(tools,"center_origin")
        self.assertEqual([call.args[:2] for call in runtime.SetTransform.call_args_list],[("parent",(-1,0,0)),("other",(1,0,0))])
        controller.History.Commit.assert_called_once();controller.SetDirty.assert_called_once()

    def test_siblings_do_not_cross_scene_roots(self):
        runtime=Mock();runtime.EditorParents={"a":"","b":"","c":""}
        runtime.EntityDetails.side_effect=lambda value:{"scene_uuid":"second" if value=="c" else "first"}
        select=Mock();tools=SimpleNamespace(Controller=SimpleNamespace(SelectedEntities=["a"]),Runtime=runtime,_Select=lambda values:select(list(values)))
        ObjectTools.Execute(tools,"select_siblings");select.assert_called_once_with(["a","b"])
