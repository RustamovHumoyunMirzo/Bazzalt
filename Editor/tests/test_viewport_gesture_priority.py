"""Gizmo overlays own drags even when unrelated geometry is behind them."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
from unittest.mock import Mock
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QMouseEvent
from PySide6.QtWidgets import QApplication
from Editor.gui.panels.viewport import NativeRenderSurface
from Editor.gui.gizmos import GizmoHandle, GizmoMode, Vec3


class ViewportGesturePriorityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_handles_win_over_unselected_geometry_for_all_transform_modes(self):
        for mode in (GizmoMode.Translate, GizmoMode.Scale, GizmoMode.Rotate):
            with self.subTest(mode=mode):
                surface = NativeRenderSurface(Mock(), True)
                surface.resize(640, 480)
                surface._selection = Vec3()
                surface._selected_entity_ids = {"selected-camera-or-light"}
                surface._mode = mode
                surface._PickGizmo = Mock(return_value=GizmoHandle.X)
                surface._PickSceneObject = Mock(return_value="unselected-plane")
                started, finished, boxes, selected, changes = [], [], [], [], []
                surface.GizmoDragStarted.connect(lambda: started.append(True))
                surface.GizmoDragFinished.connect(lambda: finished.append(True))
                surface.SelectionBoxStarted.connect(boxes.append)
                surface.EntityPicked.connect(lambda *args: selected.append(args))
                surface.TranslationDragged.connect(changes.append)
                surface.ScaleDragged.connect(changes.append)
                surface.RotationDragged.connect(lambda *args: changes.append(args))
                point = QPointF(370, 240)
                surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress, point, point, Qt.MouseButton.LeftButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier))
                self.assertIsNotNone(surface._gizmo_drag)
                self.assertIsNone(surface._selection_box_start)
                surface._PickSceneObject.assert_not_called()
                destination = QPointF(410, 260)
                surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove, destination, destination, Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier))
                self.assertFalse(surface._selection_box_dragging)
                self.assertIsNone(surface._selection_band)
                self.assertEqual(len(changes), 1)
                surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease, destination, destination, Qt.MouseButton.LeftButton, Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier))
                self.assertEqual(started, [True])
                self.assertEqual(finished, [True])
                self.assertEqual(boxes, [])
                self.assertEqual(selected, [])
                surface.close()

    def test_click_away_from_handles_keeps_object_picking_and_marquee(self):
        surface = NativeRenderSurface(Mock(), True)
        surface._selection = Vec3()
        surface._mode = GizmoMode.Translate
        surface._PickGizmo = Mock(return_value=None)
        surface._PickSceneObject = Mock(return_value="other-primitive")
        selected = []
        surface.EntityPicked.connect(lambda *args: selected.append(args))
        point = QPointF(100, 100)
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress, point, point, Qt.MouseButton.LeftButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier))
        surface.mouseReleaseEvent(QMouseEvent(QMouseEvent.Type.MouseButtonRelease, point, point, Qt.MouseButton.LeftButton, Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier))
        self.assertEqual(selected, [("other-primitive", False)])
        surface.mousePressEvent(QMouseEvent(QMouseEvent.Type.MouseButtonPress, point, point, Qt.MouseButton.LeftButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier))
        surface._PreviewSelectionBox = Mock()
        destination = QPointF(120, 120)
        surface.mouseMoveEvent(QMouseEvent(QMouseEvent.Type.MouseMove, destination, destination, Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier))
        self.assertTrue(surface._selection_box_dragging)
        surface._DestroySelectionBand()
        surface.close()
