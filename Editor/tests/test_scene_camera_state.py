import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from copy import deepcopy
from types import SimpleNamespace,MethodType
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor
from Editor.gui.panels.viewport import NativeRenderSurface
from Editor.gui.preferences import MergePreferences

class SceneCameraStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Surface=NativeRenderSurface(Mock(),True);self.Surface._fly_timer.stop()
    def tearDown(self):self.Surface.Detach();self.Surface.deleteLater();self.App.processEvents()
    def Editor(self,settings=None):
        runtime=Mock();runtime.ProjectInfo.return_value={'project_path':'C:/Projects/Game/Game.bproject'};runtime.SceneInfo.return_value={'uuid':'scene-a'}
        editor=SimpleNamespace(Runtime=runtime,Scene=SimpleNamespace(Surface=self.Surface),_settings=settings if settings is not None else {},_settings_saver=Mock(),_scene_camera_key='',_camera_restoring=False,_camera_save_timer=Mock(),Enabled=True)
        editor.PreferenceValue=lambda *_:editor.Enabled
        for name in ('_SceneCameraKey','_SyncSceneCamera','_SaveSceneCamera'):setattr(editor,name,MethodType(getattr(Editor,name),editor))
        return editor
    def test_camera_round_trip_and_invalid_data(self):
        self.Surface._target=[2.5,-3,8];self.Surface._yaw=137;self.Surface._pitch=-25;self.Surface._distance=33;self.Surface._UpdateCamera();eye=self.Surface._eye;state=self.Surface.NavigationState()
        self.Surface._target=[0,0,0];self.assertTrue(self.Surface.RestoreNavigationState(state));self.assertEqual(self.Surface.NavigationState(),state)
        for a,b in zip(self.Surface._eye,eye):self.assertAlmostEqual(a,b)
        for patch in ({'yaw':float('nan')},{'pitch':100},{'distance':0},{'target':[1,2]},{'version':99}):
            self.assertFalse(self.Surface.RestoreNavigationState({**state,**patch}));self.assertEqual(self.Surface.NavigationState(),state)
    def test_per_scene_persistence_and_disabled_preference(self):
        editor=self.Editor();editor._SyncSceneCamera();self.Surface._target=[4,5,6];editor._SaveSceneCamera();first=self.Surface.NavigationState()
        editor.Runtime.SceneInfo.return_value={'uuid':'scene-b'};editor._SyncSceneCamera();self.Surface._target=[9,8,7];editor._SaveSceneCamera()
        editor.Runtime.SceneInfo.return_value={'uuid':'scene-a'};editor._SyncSceneCamera();self.assertEqual(self.Surface.NavigationState(),first)
        stored=deepcopy(editor._settings);self.Surface._target=[0,0,0];reopened=self.Editor(stored);reopened._SyncSceneCamera();self.assertEqual(self.Surface.NavigationState(),first)
        reopened.Enabled=False;self.Surface._target=[1,1,1];reopened._SaveSceneCamera();self.assertEqual(reopened._settings,stored)
        disabled=self.Editor(stored);disabled.Enabled=False;disabled._SyncSceneCamera();self.assertEqual(self.Surface._target,[1,1,1])
    def test_preference_default_and_corrupt_store(self):
        self.assertTrue(MergePreferences(None)['scene']['save_camera_position'])
        editor=self.Editor({'scene_cameras':[]});editor._SyncSceneCamera();editor._SaveSceneCamera();self.assertIsInstance(editor._settings['scene_cameras'],dict)
