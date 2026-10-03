import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import tempfile
import unittest
import uuid
from pathlib import Path
from unittest.mock import patch
from PySide6.QtCore import QEvent,Qt
from PySide6.QtGui import QKeyEvent
from PySide6.QtWidgets import QApplication
from PySide6.QtTest import QTest
from Editor.gui.application import Editor
from Editor.game_input import KeyScancode

class GameInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):self.Window=Editor()
    def tearDown(self):
        self.Window.Controller.Stop();self.Window.Controller.SetDirty(False);self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_key_translation(self):
        self.assertEqual(KeyScancode(QKeyEvent(QEvent.Type.KeyPress,Qt.Key.Key_W,Qt.KeyboardModifier.NoModifier)),26)
        self.assertEqual(KeyScancode(QKeyEvent(QEvent.Type.KeyPress,Qt.Key.Key_1,Qt.KeyboardModifier.KeypadModifier)),89)
    def test_play_opens_output_and_stop_restores_transforms_and_history(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Object");w.Runtime.AddComponent(entity,"Primitive Object");w.Controller.SelectEntity(entity)
        w.Controller._Mutate("Before Play",lambda:w.Runtime.SetTransform(entity,[1,2,3],[0,0,0,1],[1,1,1]));w.Controller.SetDirty(True)
        checkpoint=w.Controller.History.Checkpoint();w.Docking.close_panel("output")
        w.Controller.Play();self.assertTrue(w.Runtime.IsPlaying());self.assertTrue(w.Docking.is_panel_open("output"))
        self.assertTrue(w.Runtime.SetTransform(entity,[10,20,30],[0,0,0,1],[2,2,2]));w.Runtime.Tick()
        self.assertEqual(tuple(w.Runtime.EntityDetails(entity)["position"]),(10,20,30))
        w.ToggleGameMaximized();self.assertEqual(w.Docking.save_layout()["root"]["panels"],["output"])
        w.ToggleGameMaximized();self.assertTrue(w.Docking.is_panel_open("scene"))
        w.Controller.Stop();self.assertEqual(tuple(w.Runtime.EntityDetails(entity)["position"]),(1,2,3));self.assertEqual(w.Controller.History.Checkpoint(),checkpoint);self.assertTrue(w.Controller.IsDirty)
    def test_native_input_is_gated_and_resets_on_pause_focus_loss_and_stop(self):
        w=self.Window;self.assertTrue(w.Runtime.Play());host=w.Runtime._host
        host.game_key(26,True,False);w.Runtime.Tick();self.assertFalse(host.input_snapshot()["active"])
        host.set_game_input_active(True);host.game_key(26,True,False);w.Runtime.Tick();self.assertIn(26,host.input_snapshot()["down"])
        w.Runtime.Tick();self.assertIn(26,host.input_snapshot()["keys"]);self.assertNotIn(26,host.input_snapshot()["down"])
        host.set_game_input_active(False);self.assertEqual(host.input_snapshot()["keys"],[])
        host.set_game_input_active(True);host.game_key(26,True,False);w.Runtime.Pause(True);self.assertFalse(host.input_snapshot()["active"])
        w.Runtime.Pause(False);host.set_game_input_active(True);w.Runtime.Tick();self.assertEqual(host.input_snapshot()["keys"],[])
        w.Runtime.Stop();self.assertFalse(host.input_snapshot()["active"])
    def test_game_surface_routes_keys_blocks_editor_shortcuts_and_maximizes(self):
        w=self.Window;w.show();self.App.setActiveWindow(w);w.Controller.Play();self.App.processEvents()
        surface=w.Output._output_stack.currentWidget();surface.setFocus();self.assertTrue(w.Controller.GameInput.SyncFocus())
        QTest.keyPress(surface,Qt.Key.Key_W);w.Runtime.Tick();self.assertIn(26,w.Runtime._host.input_snapshot()["keys"])
        QTest.keyRelease(surface,Qt.Key.Key_W);w.Runtime.Tick();self.assertIn(26,w.Runtime._host.input_snapshot()["up"])
        with patch.object(w.Controller,"FrameAll") as frame:
            QTest.keyClick(surface,Qt.Key.Key_A);frame.assert_not_called()
        QTest.keyClick(surface,Qt.Key.Key_Space,Qt.KeyboardModifier.ShiftModifier)
        self.assertEqual(w.Docking.save_layout()["root"]["panels"],["output"])
        QTest.keyClick(surface,Qt.Key.Key_Space,Qt.KeyboardModifier.ShiftModifier);self.assertTrue(w.Docking.is_panel_open("scene"))
        w.Hierarchy.Tree.setFocus();self.assertFalse(w.Controller.GameInput.SyncFocus());self.assertFalse(w.Runtime._host.input_snapshot()["active"])
    def test_folder_tree_rename_with_loaded_scene_keeps_scene_identity(self):
        w=self.Window
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);assets=root/"Assets";directory=assets/"Scenes";directory.mkdir(parents=True)
            project=root/"Test.bproject";project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Test"\nAssetDirectory: "Assets"\nStartupScene: ""\nProperties:\n',encoding="utf-8")
            self.assertTrue(w.Runtime.LoadProject(project));scene=directory/"First.bscene";self.assertTrue(w.Runtime.SaveScene(scene));scene_id=w.Runtime.SceneInfo()["uuid"]
            w.AssetBrowser.Refresh();item=w.AssetBrowser._FindDirectoryItem(w.AssetBrowser.Tree.topLevelItem(0),directory)
            self.assertTrue(item.flags()&Qt.ItemFlag.ItemIsEditable);w.AssetBrowser.Tree.setCurrentItem(item);item.setText(0,"Renamed")
            renamed=assets/"Renamed"/"First.bscene";self.assertTrue(renamed.is_file());self.assertEqual(Path(w.Runtime.SceneInfo()["path"]),renamed)
            self.assertEqual(w.Runtime.SceneInfo()["uuid"],scene_id);self.assertTrue(w.Runtime.IsSceneLoaded(renamed));w.Runtime.Release()
    def test_stop_restores_inactive_scene_authoring_values(self):
        w=self.Window
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);assets=root/"Assets";assets.mkdir()
            project=root/"Test.bproject";project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Test"\nAssetDirectory: "Assets"\nStartupScene: ""\nProperties:\n',encoding="utf-8")
            self.assertTrue(w.Runtime.LoadProject(project));scene_id=str(uuid.uuid4());path=assets/"Other.bscene"
            path.write_text(f'FormatVersion: 1\nSceneUUID: "{scene_id}"\nEntities:\n',encoding="utf-8")
            self.assertTrue(w.Runtime.LoadSceneAdditive(path));entity=w.Runtime.CreateEntity("Inactive",scene_id)
            self.assertTrue(w.Runtime.SetTransform(entity,[1,2,3],[0,0,0,1],[1,1,1]));self.assertTrue(w.Runtime.Play())
            self.assertTrue(w.Runtime.SetTransform(entity,[8,9,10],[0,0,0,1],[2,2,2]));w.Runtime.Stop()
            self.assertEqual(tuple(w.Runtime.EntityDetails(entity)["position"]),(1,2,3));w.Runtime.Release()

if __name__=="__main__":unittest.main()
