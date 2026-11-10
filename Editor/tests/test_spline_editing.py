"""Scene spline authoring and modal translation regressions."""
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from PySide6.QtCore import QCoreApplication, QEvent, QPoint, QPointF, Qt
from PySide6.QtGui import QKeyEvent, QMouseEvent
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor
from Editor.gui.gizmos import Vec3


class SplineEditingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App=QApplication.instance() or QApplication([])

    def setUp(self):
        self.Window=Editor()
        if not self.Window.Runtime.IsAvailable():self.skipTest('Native bridge is unavailable')
        self.Runtime=self.Window.Runtime
        self.Surface=self.Window.Scene.Surface
        self.Surface.resize(640,480)
        self.Surface._eye=(0.,8.,10.)
        self.Surface._target=[0.,0.,0.]

    def tearDown(self):
        self.Window.Controller.SetDirty(False)
        self.Window.close()
        self.Window.deleteLater()
        self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

    def Key(self, key):
        event=QKeyEvent(QEvent.Type.KeyPress,key,Qt.KeyboardModifier.NoModifier)
        QCoreApplication.sendEvent(self.Surface,event)

    def Mouse(self, kind, point, button=Qt.MouseButton.NoButton, buttons=Qt.MouseButton.NoButton):
        event=QMouseEvent(kind,QPointF(point),QPointF(point),button,buttons,Qt.KeyboardModifier.NoModifier)
        QCoreApplication.sendEvent(self.Surface,event)

    def Click(self, point):
        self.Mouse(QEvent.Type.MouseButtonPress,point,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton)
        self.Mouse(QEvent.Type.MouseButtonRelease,point,Qt.MouseButton.LeftButton)

    def Project(self, point):
        projected=self.Surface._Project(point)
        return QPoint(round(projected[0]),round(projected[1]))

    def test_unchanged_ticks_do_not_rebuild_gizmos_but_moving_entities_do(self):
        from unittest.mock import patch
        entity=self.Runtime.CreateEntity('Tracked')
        self.Window.Controller.SelectEntity(entity)
        with patch.object(self.Runtime,'SetGizmo',wraps=self.Runtime.SetGizmo) as gizmo, patch.object(self.Runtime,'SetSelectionOutline',wraps=self.Runtime.SetSelectionOutline) as outline:
            for _ in range(100):self.Window.Controller._UpdateGizmo()
            self.assertEqual(gizmo.call_count,0)
            self.assertEqual(outline.call_count,0)
            self.Runtime.SetTransform(entity,(1,2,3),(0,0,0,1),(1,1,1))
            self.Window.Controller._UpdateGizmo()
            self.assertEqual(gizmo.call_count,1)
            self.assertEqual((self.Surface._selection.X,self.Surface._selection.Y,self.Surface._selection.Z),(1.,2.,3.))

    def test_grab_constraints_cancel_and_apply_are_one_undo_step(self):
        entity=self.Runtime.CreateEntity('Move me')
        self.Window.Controller.SelectEntity(entity)
        self.Window.Controller.SetDirty(False)
        self.Surface._scene_pointer=QPoint(320,240)
        self.Key(Qt.Key.Key_G)
        self.assertTrue(self.Surface._grab_active)
        self.Mouse(QEvent.Type.MouseMove,QPoint(380,210))
        self.assertNotEqual(tuple(self.Runtime.EntityPose(entity)['position']),(0.,0.,0.))
        self.Key(Qt.Key.Key_X)
        constrained=self.Runtime.EntityPose(entity)['position']
        self.assertAlmostEqual(constrained[1],0.,places=5)
        self.assertAlmostEqual(constrained[2],0.,places=5)
        self.Key(Qt.Key.Key_Escape)
        self.assertEqual(tuple(self.Runtime.EntityPose(entity)['position']),(0.,0.,0.))
        self.assertFalse(self.Window.Controller.IsDirty)
        self.assertFalse(self.Window.Controller.History.CanUndo())
        self.Key(Qt.Key.Key_G)
        self.Mouse(QEvent.Type.MouseMove,QPoint(420,210))
        applied=tuple(self.Runtime.EntityPose(entity)['position'])
        self.Click(QPoint(420,210))
        self.assertFalse(self.Surface._grab_active)
        self.assertEqual(tuple(self.Runtime.EntityPose(entity)['position']),applied)
        self.Window.Controller.Undo()
        self.assertEqual(tuple(self.Runtime.EntityPose(entity)['position']),(0.,0.,0.))
        self.Window.Controller.Redo()
        self.assertEqual(tuple(self.Runtime.EntityPose(entity)['position']),applied)

    def test_pen_preview_handles_and_point_grab_roundtrip(self):
        entity=self.Runtime.CreateEntity('Ground')
        self.Runtime.AddComponent(entity,'Terrain')
        self.Window.Controller.SelectEntity(entity)
        owner=self.Window.Controller.TerrainEditor
        self.Window.Scene.TerrainAction.setChecked(True)
        owner.SetTool(9)
        editor=owner.SplineEditor
        self.assertIs(editor.Overlay.parentWidget(),self.Surface)
        self.assertTrue(editor.ControlsVisible)
        self.Key(Qt.Key.Key_Tab)
        self.assertFalse(editor.ControlsVisible)
        self.assertTrue(editor.IsActive())
        owner.SplineControls.click()
        self.assertTrue(editor.ControlsVisible)

        self.Click(self.Project((-2,0,0)))
        self.assertEqual(len(editor.Data['points']),1)
        self.Mouse(QEvent.Type.MouseMove,self.Project((2,0,0)))
        self.assertIsNotNone(editor.Preview)
        self.Click(self.Project((2,0,0)))
        self.assertEqual(len(editor.Data['points']),2)
        self.Key(Qt.Key.Key_Escape)
        editor.Select(0)
        before=tuple(editor.Data['points'][0])
        editor.Shape.setCurrentIndex(1)
        self.assertNotEqual(editor.Data['outgoing'][0],editor.Data['points'][0])
        self.assertGreater(len(editor.Data['path']),2)
        self.Surface._scene_pointer=self.Project(before)
        self.Key(Qt.Key.Key_G)
        self.Mouse(QEvent.Type.MouseMove,self.Surface._scene_pointer+QPoint(25,-15))
        self.assertNotEqual(tuple(editor.Data['points'][0]),before)
        self.Key(Qt.Key.Key_Escape)
        self.assertEqual(tuple(editor.Data['points'][0]),before)
        self.assertEqual(tuple(self.Runtime.EntityPose(entity)['position']),(0.,0.,0.))
        editor.Select(0,2)
        handle=tuple(editor.Data['outgoing'][0])
        point=self.Project(handle)
        self.Mouse(QEvent.Type.MouseButtonPress,point,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton)
        self.Mouse(QEvent.Type.MouseMove,point+QPoint(15,-20),buttons=Qt.MouseButton.LeftButton)
        self.Mouse(QEvent.Type.MouseButtonRelease,point+QPoint(15,-20),Qt.MouseButton.LeftButton)
        self.assertNotEqual(tuple(editor.Data['outgoing'][0]),handle)
        snapshot=self.Runtime.CaptureScene()
        self.assertIn(b'.Out',snapshot)
        self.Window.Controller.Undo()
        self.assertAlmostEqual(editor.Data['outgoing'][0][2],handle[2],places=5)

    def test_world_handle_movement_respects_terrain_transform(self):
        entity=self.Runtime.CreateEntity('Scaled ground')
        self.Runtime.AddComponent(entity,'Terrain')
        self.Runtime.SetTransform(entity,(3.,0.,0.),(0.,0.,0.,1.),(2.,1.,3.))
        self.Runtime.TerrainCommand(entity,'spline_add',dict(spline=0,origin=(3.,10.,0.),direction=(0.,-1.,0.)))
        self.Runtime.TerrainCommand(entity,'spline_move',dict(spline=0,point=0,part=0,position=(7.,2.,6.)))
        data=self.Runtime.TerrainCommand(entity,'spline_info',{'spline':0})
        self.assertEqual(tuple(data['points'][0]),(7.,2.,6.))
        fields=self.Runtime.EntityDetails(entity)['component_data']['Terrain']['Splines'][0]
        self.assertEqual(tuple(fields['Points'][0]),(2.,2.,2.))
