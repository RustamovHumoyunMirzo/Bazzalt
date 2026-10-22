import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import tempfile
from contextlib import ExitStack
from pathlib import Path
import unittest
from unittest.mock import Mock
from math import isfinite
from PySide6.QtCore import Qt,QPoint,QPointF,QEvent
from PySide6.QtGui import QMouseEvent,QWheelEvent
from PySide6.QtWidgets import QApplication,QWidget,QVBoxLayout,QScrollArea,QPushButton
from Editor.gui.gizmos import GizmoMode,GizmoHandle,GizmoDelta,Vec3
from Editor.gui.transform_trails import TransformTrail
from Editor.gui.panels.viewport import NativeRenderSurface
from Editor.gui.panels.audio_mixer import AudioMixerPanel,MixerFader,ZERO
from Editor.gui.widgets.fields import FloatInput,EnumInput
from Editor.gui.wheel_policy import EditorWheelPolicy
from Editor.gui.preferences import MergePreferences
from Editor.runtime import RuntimeService
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager

class MixerSceneInteractionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_transform_trail_geometry_is_bounded(self):
        trail=TransformTrail(Vec3(1,2,3),2)
        for i in range(1000):lines=trail.Build(GizmoMode.Translate,GizmoDelta(Translation=Vec3(i*.1,0,0)))
        self.assertLessEqual(len(trail.Points),256);self.assertEqual(trail.Points[0],Vec3(1,2,3))
        for mode,result in ((GizmoMode.Translate,GizmoDelta(Translation=Vec3(4,2,0))),(GizmoMode.Rotate,GizmoDelta(RotationAxis=Vec3(0,1,0),RotationRadians=2)),(GizmoMode.Scale,GizmoDelta(Scale=Vec3(2,1,.5)))):
            lines=trail.Build(mode,result);self.assertTrue(lines);self.assertLess(len(lines),2048);self.assertTrue(all(len(line)==10 and all(isfinite(n) for n in line) for line in lines))
        preferences=MergePreferences({});self.assertTrue(preferences['scene']['trail_translate']);self.assertEqual(preferences['scene']['trail_duration'],1.5)
    def test_custom_fader_and_narrow_layout(self):
        fader=MixerFader();fader.resize(100,190);fader.setValue(400);self.assertFalse(fader.grab().isNull())
        fader.mouseDoubleClickEvent(self._Mouse(QEvent.Type.MouseButtonDblClick,40,60,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton));self.assertEqual(fader.value(),800)
        panel=AudioMixerPanel(Mock(),LocalizationManager(ResourceManager()));panel.resize(320,420);panel.show();self.App.processEvents();self.assertEqual(panel.Splitter.orientation(),Qt.Orientation.Vertical)
        panel.resize(900,420);self.App.processEvents();self.assertEqual(panel.Splitter.orientation(),Qt.Orientation.Horizontal)
        panel.SyncTimer.stop();panel.close();panel.deleteLater();fader.deleteLater()
    def _Surface(self,mode):
        runtime=Mock();surface=NativeRenderSurface(runtime,True);surface.resize(640,480);surface.SetSelection((0,0,0));surface.SetGizmoMode(mode);surface._selected_entity_ids={'cube'}
        surface._PickSceneObject=Mock(return_value='cube');surface._PickSceneIcon=Mock(return_value='');surface._PickGizmo=Mock(return_value=None);surface._PickOrientation=Mock(return_value=False)
        return surface,runtime
    def _Mouse(self,kind,x,y,button,buttons,modifiers=Qt.KeyboardModifier.NoModifier):
        return QMouseEvent(kind,QPointF(x,y),QPointF(x,y),button,buttons,modifiers)
    def test_free_move_works_in_every_manipulation_mode(self):
        for mode in (GizmoMode.Translate,GizmoMode.Rotate,GizmoMode.Scale):
            with self.subTest(mode=mode):
                surface,runtime=self._Surface(mode);moved=[];finished=[];surface.TranslationDragged.connect(moved.append);surface.GizmoDragFinished.connect(lambda:finished.append(True))
                surface.mousePressEvent(self._Mouse(QEvent.Type.MouseButtonPress,320,240,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton));self.assertIsNone(surface._selection_box_start)
                surface.mouseMoveEvent(self._Mouse(QEvent.Type.MouseMove,350,250,Qt.MouseButton.NoButton,Qt.MouseButton.LeftButton));self.assertTrue(moved);self.assertGreater(moved[0].Length(),0);self.assertEqual(surface._gizmo_drag.Mode,GizmoMode.Translate)
                self.assertTrue(runtime.SetTransformTrail.call_args.args[0]);surface.mouseReleaseEvent(self._Mouse(QEvent.Type.MouseButtonRelease,350,250,Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton));self.assertEqual(finished,[True]);surface._fly_timer.stop();surface.ClearTransformTrail();surface.deleteLater()
    def test_select_mode_and_gizmo_precedence_are_preserved(self):
        surface,_=self._Surface(GizmoMode.Select);surface.mousePressEvent(self._Mouse(QEvent.Type.MouseButtonPress,320,240,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton));self.assertIsNone(surface._free_move_pending);self.assertIsNotNone(surface._selection_box_start);surface._fly_timer.stop();surface.deleteLater()
        surface,_=self._Surface(GizmoMode.Scale);surface._PickGizmo.return_value=GizmoHandle.X;surface.mousePressEvent(self._Mouse(QEvent.Type.MouseButtonPress,320,240,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton));self.assertEqual(surface._gizmo_drag.Mode,GizmoMode.Scale);self.assertIsNone(surface._free_move_pending);surface._EndTransformDrag();surface.ClearTransformTrail();surface._fly_timer.stop();surface.deleteLater()
    def test_unfocused_fields_forward_wheel_to_scroll_area(self):
        editor=QWidget();layout=QVBoxLayout(editor);area=QScrollArea();area.setWidgetResizable(True);body=QWidget();content=QVBoxLayout(body);field=FloatInput(value=3);combo=EnumInput();combo.SetOptions((('A',0),('B',1)));content.addWidget(field);content.addWidget(combo);content.addStretch();body.setMinimumHeight(1800);area.setWidget(body);layout.addWidget(area);button=QPushButton('focus');layout.addWidget(button)
        policy=EditorWheelPolicy(editor);self.App.installEventFilter(policy);editor.resize(320,300);editor.show();editor.activateWindow();button.setFocus();self.App.processEvents()
        try:
            scroll=area.verticalScrollBar();scroll.setValue(0)
            for control in (field,combo):
                event=QWheelEvent(QPointF(10,10),QPointF(control.mapToGlobal(QPoint(10,10))),QPoint(),QPoint(0,-120),Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.NoScrollPhase,False);QApplication.sendEvent(control,event)
            self.assertEqual(field.value(),3);self.assertEqual(combo.GetValue(),0);self.assertGreater(scroll.value(),0)
            field.setFocus();self.App.processEvents();self.assertTrue(field.hasFocus());event=QWheelEvent(QPointF(10,10),QPointF(field.mapToGlobal(QPoint(10,10))),QPoint(),QPoint(0,120),Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.NoScrollPhase,False);QApplication.sendEvent(field,event);self.assertNotEqual(field.value(),3)
        finally:self.App.removeEventFilter(policy);editor.close();editor.deleteLater()
    def test_mixer_panel_edits_real_asset_and_protects_external_changes(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime is not built')
        with tempfile.TemporaryDirectory() as directory,ExitStack() as cleanup:
            cleanup.callback(runtime.Release);root=Path(directory);(root/'Assets').mkdir();project=root/'Audio.bproject';project.write_text('FormatVersion: 1\nProjectUUID: "12345678-1234-4234-8234-123456789abc"\nName: Audio\nAssetDirectory: Assets\nStartupScene: ""\nProperties:\n',encoding='utf-8')
            mixer=root/'Assets'/'Test.bmixer';mixer.write_text(f'Version: 1\nBuses:\n  - Id: "{ZERO}"\n    Parent: "{ZERO}"\n    Name: "Master"\n',encoding='utf-8');self.assertTrue(runtime.LoadProject(str(project)),runtime.LastError())
            panel=AudioMixerPanel(runtime,LocalizationManager(ResourceManager()));cleanup.callback(panel.deleteLater);cleanup.callback(panel.SyncTimer.stop);cleanup.callback(panel.SaveTimer.stop)
            info=runtime.AssetInfo(str(mixer));self.assertTrue(panel.Open(info['uuid']));self.assertEqual(len(panel.Strips),1);panel.AddBus();self.assertEqual(len(panel.Strips),2);bus=panel.Selected();panel.Edit(bus,'volume',.25);self.assertTrue(panel.Flush());self.assertEqual(len(runtime.AudioBuses(info['uuid'])),2)
            panel.Route(ZERO,bus);self.assertEqual(panel._Bus(ZERO)['parent'],ZERO)
            panel.Edit(bus,'pitch',1.4);external=mixer.read_text(encoding='utf-8')+'\n# External edit\n';mixer.write_text(external,encoding='utf-8');self.assertFalse(panel.Flush());self.assertEqual(mixer.read_text(encoding='utf-8'),external)
            panel.ReloadAsset();self.assertFalse(panel.Pending);self.assertEqual(len(panel.Strips),2);self.assertEqual(panel.Error.text(),'');self.assertEqual(mixer.read_text(encoding='utf-8'),external)
