import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import json,tempfile,unittest,uuid
from pathlib import Path
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt,QMimeData,QPointF
from PySide6.QtGui import QDropEvent
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.runtime import RuntimeService
from Editor.history import SceneHistory
class PrefabTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_hierarchy_drop_is_a_prefab_request_not_file_import(self):
        with tempfile.TemporaryDirectory() as folder:
            resources=ResourceManager();panel=AssetBrowserPanel(LocalizationManager(resources),resources);panel.SetProjectRoot(folder);panel.resize(600,350);panel.show();self.App.processEvents();received=[];panel.EntityDropped.connect(lambda entity,directory:received.append((entity,directory)));mime=QMimeData();mime.setData('application/x-bazzalt-entity',b'entity-id');event=QDropEvent(QPointF(550,290),Qt.DropAction.CopyAction,mime,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier);panel.Browser.dropEvent(event);self.assertTrue(event.isAccepted());self.assertEqual(received,[('entity-id',Path(folder).resolve())]);panel.deleteLater()
    def test_native_asset_instances_multi_scene_undo_and_source_apply(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        directory=tempfile.TemporaryDirectory();self.addCleanup(directory.cleanup);self.addCleanup(runtime.Release)
        folder=directory.name
        if folder:
            root=Path(folder);assets=root/'Assets';assets.mkdir();project=root/'Game.bproject';project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Test\nAssetDirectory: Assets\nStartupScene: ""\n',encoding='utf-8');self.assertTrue(runtime.LoadProject(project));entity=runtime.CreateEntity('Reusable');child=runtime.CreateEntity('Child',entity);runtime.AddComponent(child,'Primitive Object');path=assets/'Reusable.bprefab';self.assertTrue(runtime.SavePrefab(entity,path));self.assertFalse(runtime.PrefabInfo(entity)['overrides']);history=SceneHistory(runtime);history.BeginScenes('Instantiate prefab');instance=runtime.InstantiatePrefabPath(path);self.assertTrue(instance);self.assertTrue(history.Commit());self.assertTrue(history.Undo());self.assertFalse(runtime.EntityDetails(instance));self.assertTrue(history.Redo());self.assertTrue(runtime.EntityDetails(instance));self.assertTrue(runtime.Rename(instance,'Local override'));self.assertTrue(runtime.PrefabInfo(instance)['overrides']);self.assertEqual(runtime.PrefabOperation(instance,'revert'),instance);self.assertEqual(runtime.EntityDetails(instance)['name'],'Reusable');self.assertTrue(runtime.Rename(instance,'Applied name'));self.assertEqual(runtime.PrefabOperation(instance,'apply'),instance);self.assertEqual(runtime.EntityDetails(entity)['name'],'Applied name');self.assertEqual(runtime.PrefabOperation(instance,'unpack'),instance);self.assertEqual(runtime.PrefabInfo(instance),{});runtime.Release()
if __name__=='__main__':unittest.main()
