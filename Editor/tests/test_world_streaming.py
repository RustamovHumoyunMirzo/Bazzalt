import os,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from Editor.runtime import RuntimeService
from pathlib import Path
import tempfile,uuid,time
from PySide6.QtWidgets import QApplication
class WorldStreamingInspectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_components_and_validation(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        try:
            entity=runtime.CreateEntity('World anchor')
            self.assertTrue(runtime.AddComponent(entity,'World Cell'));self.assertTrue(runtime.AddComponent(entity,'Streaming Source'))
            self.assertTrue(runtime.SetComponentProperty(entity,'World Cell','Half Extents',[100,50,100]));self.assertFalse(runtime.SetComponentProperty(entity,'World Cell','Half Extents',[-1,50,100]));self.assertFalse(runtime.SetComponentProperty(entity,'Streaming Source','Distance Scale',0));self.assertTrue(runtime.SetComponentProperty(entity,'Streaming Source','Distance Scale',2))
            details=runtime.EntityDetails(entity)['component_data'];self.assertEqual(list(details['World Cell']['Half Extents']),[100,50,100]);self.assertEqual(details['Streaming Source']['Distance Scale'],2)
            self.assertTrue(runtime.RemoveComponent(entity,'World Cell'));self.assertTrue(runtime.RemoveComponent(entity,'Streaming Source'))
        finally:runtime.Release()

    def test_play_streamed_entities_are_not_saved_into_authoring_scene(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        try:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);assets=root/'Assets';assets.mkdir();project=root/'World.bproject';project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Streaming\nAssetDirectory: Assets\nStartupScene: ""\n')
                self.assertTrue(runtime.LoadProject(project));chunk=runtime.CreateEntity('STREAMED_RUNTIME_ONLY');self.assertTrue(runtime.SaveScene(assets/'Chunk.bscene'));runtime.NewScene();self.assertTrue(runtime.RefreshAssets());asset=runtime.ResolveAssetRecord(assets/'Chunk.bscene');self.assertIsNotNone(asset)
                anchor=runtime.CreateEntity('Persistent world definition');self.assertTrue(runtime.AddComponent(anchor,'World Cell'));self.assertTrue(runtime.SetComponentProperty(anchor,'World Cell','Scene Asset',asset['uuid']));self.assertTrue(runtime.SetComponentProperty(anchor,'World Cell','Always Loaded',True));path=assets/'World.bscene';self.assertTrue(runtime.SaveScene(path));self.assertTrue(runtime.Play())
                for _ in range(300):
                    runtime.Tick()
                    if any(e['name']=='STREAMED_RUNTIME_ONLY' for e in runtime.Entities()):break
                    time.sleep(.001)
                self.assertTrue(any(e['name']=='STREAMED_RUNTIME_ONLY' for e in runtime.Entities()));self.assertTrue(runtime.SaveScene(path));self.assertNotIn('STREAMED_RUNTIME_ONLY',path.read_text());self.assertIn('Persistent world definition',path.read_text());runtime.Stop();self.assertFalse(any(e['name']=='STREAMED_RUNTIME_ONLY' for e in runtime.Entities()));runtime.Release()
        finally:runtime.Stop();runtime.Release()
