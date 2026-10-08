import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
from pathlib import Path
from unittest.mock import Mock
from PySide6.QtCore import QPointF,QEvent,Qt
from PySide6.QtGui import QMouseEvent
from PySide6.QtWidgets import QApplication,QLabel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.gui.display_names import Humanize,DisplayName
from Editor.gui.transform_units import TransformUnits,SnapValue,SnapRotation
from Editor.gui.preferences import MergePreferences,STATISTIC_FIELDS
from Editor.gui.panels.properties import PropertiesPanel
from Editor.gui.panels.viewport import StatisticsText,NativeRenderSurface
from Editor.gui.gizmos import GizmoDelta,Vec3,GizmoMode

class TodoEditorToolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_spaced_localized_labels_keep_identifiers(self):
        locale=LocalizationManager(ResourceManager(Path(__file__).parents[1]/"assets"))
        self.assertEqual(Humanize("GuiTextInput"),"GUI Text Input")
        panel=PropertiesPanel(locale);section=panel.AddComponentSection("runtime.GuiText","GuiText")
        section.AddField("FontSize",QLabel("16"));self.assertEqual(section.Toggle.text(),"GUI Text")
        self.assertEqual(section._captions["FontSize"].text(),"Font Size");self.assertIn("FontSize",section._fields)
        locale._messages["component.GuiText"]="Translated text";locale._messages["field.FontSize"]="Translated size";locale.LocaleChanged.emit("en")
        self.assertEqual(section.Toggle.text(),"Translated text");self.assertEqual(section._captions["FontSize"].text(),"Translated size")
        self.assertEqual(DisplayName(locale,"CustomSpeedComponent"),"Custom Speed Component");panel.deleteLater()
    def test_units_validation_and_shared_increment(self):
        p=MergePreferences({"transform":{"translation_step":2,"rotation_step":float("nan"),"scale_step":0}})
        self.assertEqual(p["transform"]["translation_step"],2);self.assertEqual(p["transform"]["rotation_step"],15);self.assertEqual(p["transform"]["scale_step"],.1)
        self.assertEqual(SnapValue(3.3,p["transform"]["translation_step"]),4)
        self.assertEqual(TransformUnits({"translation_step":"bad"})["translation_step"],.5)
    def test_statistics_are_native_only_and_configurable(self):
        runtime=Mock();text=StatisticsText(runtime);text.setText("60 FPS");text.setVisible(True)
        runtime.SetEditorStatisticsText.assert_called_with("60 FPS",True)
        self.assertEqual(text.text(),"60 FPS");self.assertTrue(text.isVisible())
        prefs=MergePreferences({"statistics":{"fps":False,"gui_batches":True}})
        self.assertFalse(prefs["statistics"]["fps"]);self.assertTrue(prefs["statistics"]["gui_batches"])
        locale=LocalizationManager(ResourceManager(Path(__file__).parents[1]/"assets"))
        for key in STATISTIC_FIELDS:
            self.assertNotEqual(locale.Translate("statistics.label."+key),"statistics.label."+key)
            self.assertIn("9",locale.Translate("statistics.value."+key,value=9))
    def test_drag_quantizes_total_not_each_mouse_event(self):
        surface=NativeRenderSurface(Mock(),True);surface.TransformUnits=TransformUnits({"translation_step":.5,"snapping_enabled":True});surface._mode=GizmoMode.Translate
        surface._gizmo_drag=Mock();surface._gizmo_drag.Calculate.side_effect=[GizmoDelta(Translation=Vec3(.2,0,0)),GizmoDelta(Translation=Vec3(.4,0,0))]
        values=[];surface.TranslationDragged.connect(lambda value:values.append(value.X))
        for x in (20,21):surface.mouseMoveEvent(QMouseEvent(QEvent.Type.MouseMove,QPointF(x,30),QPointF(x,30),QPointF(x,30),Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier))
        self.assertEqual(values,[0,.5]);self.assertEqual(surface._scene_pointer.x(),21);surface.deleteLater()
    def test_rotation_snap_preserves_normalized_quaternion(self):
        import math
        q=SnapRotation((0,math.sin(math.radians(14)*.5),0,math.cos(math.radians(14)*.5)),15)
        self.assertAlmostEqual(sum(value*value for value in q),1)
        self.assertAlmostEqual(q[1],math.sin(math.radians(15)*.5))
