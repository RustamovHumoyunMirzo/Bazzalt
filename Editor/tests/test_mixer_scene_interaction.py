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
from Editor.gui.panels.audio_mixer import AudioMixerPanel,ZERO
from Editor.gui.panels.mixer_controls import FaderWidget
from Editor.theme import Theme
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
    def test_browser_refresh_keeps_multi_selection(self):
        from Editor.gui.panels.assets import AssetBrowserPanel
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for name in ('A.bmixer','B.bmixer'):(root/name).write_text('Version: 1',encoding='utf-8')
            panel=AssetBrowserPanel(LocalizationManager(ResourceManager()),ResourceManager());panel.SetProjectRoot(root)
            for index in range(panel.Browser.count()):panel.Browser.item(index).setSelected(True)
            cleared=[];panel.SelectionCleared.connect(lambda:cleared.append(True));panel.Refresh();self.assertEqual(len(panel.Browser.selectedItems()),2);self.assertEqual(cleared,[]);panel.deleteLater()
    def test_custom_fader_and_narrow_layout(self):
        fader=FaderWidget(Theme.dark());fader.resize(100,190);fader.SetValue(-40);self.assertFalse(fader.grab().isNull())
        fader.mouseDoubleClickEvent(self._Mouse(QEvent.Type.MouseButtonDblClick,40,60,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton));self.assertEqual(fader.GetValue(),0)
        panel=AudioMixerPanel(Mock(),LocalizationManager(ResourceManager()));panel.resize(320,420);panel.show();self.App.processEvents();self.assertLessEqual(panel.minimumWidth(),320)
        panel.resize(900,420);self.App.processEvents();self.assertEqual(panel.Controls._splitter.orientation(),Qt.Orientation.Horizontal)
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
            info=runtime.AssetInfo(str(mixer));self.assertTrue(panel.Open(info['uuid']),panel.Error.text());controls=panel.Controls;self.assertEqual(len(controls.GetGroups()),1);controls.AddGroup('Music');controls.SetVolume('Music',-12);effect=controls.AddEffect('Music','Lowpass');controls.SetEffectParameter('Music',effect,'Cutoff Freq',700);controls.CreateSnapshot('Dull');controls.CreateView('Music only');self.assertTrue(panel.Flush(),runtime.LastError());self.assertEqual(len(runtime.AudioBuses(info['uuid'])),2)
            bus_id=panel.BusIds['Music'];controls.RenameGroup('Music','Renamed');self.assertTrue(panel.Flush());self.assertEqual(panel.BusIds['Renamed'],bus_id)
            panel.ReloadAsset();self.assertEqual(panel.Controls.GetEffectParameter('Renamed',effect,'Cutoff Freq'),700);self.assertIn('Dull',panel.Controls.GetSnapshots());self.assertIn('Music only',panel.Controls.GetViews())
            panel.Controls.SetVolume('Renamed',-8);external=mixer.read_text(encoding='utf-8')+'\n# External edit\n';mixer.write_text(external,encoding='utf-8');self.assertFalse(panel.Flush());self.assertEqual(mixer.read_text(encoding='utf-8'),external)
            panel.ReloadAsset();self.assertFalse(panel.Pending);self.assertEqual(len(panel.Controls.GetGroups()),2);self.assertEqual(panel.Error.text(),'');self.assertEqual(mixer.read_text(encoding='utf-8'),external)
