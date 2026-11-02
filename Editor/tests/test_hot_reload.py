import os,tempfile,unittest,uuid,threading
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtCore import QObject,QThread
from shiboken6 import delete
from PySide6.QtWidgets import QApplication
from PySide6.QtTest import QTest
from Editor.hot_reload import HotReloadController,_Compile
from Editor.scripting import BuildResult

class HotReloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_compile_is_background_and_notifications_are_on_ui_thread(self):
        entered=threading.Event();release=threading.Event();threads=[]
        def build(sources):entered.set();release.wait(3);return BuildResult(True)
        compiler=SimpleNamespace(Build=build);worker=_Compile(compiler,[],None)
        class Receiver(QObject):
            def Done(self,result):threads.append(QThread.currentThread());self.result=result
        receiver=Receiver();worker.Completed.connect(receiver.Done);worker.start()
        self.assertTrue(entered.wait(2));self.assertTrue(worker.isRunning());release.set()
        for _ in range(200):
            self.App.processEvents()
            if threads:break
            QTest.qWait(5)
        self.assertEqual(threads,[self.App.thread()]);self.assertTrue(worker.wait(3000));worker.deleteLater()
    def test_stopped_play_discards_compilation_results(self):
        controller=QObject();controller.Runtime=Mock();controller.Runtime.IsPlaying.return_value=True
        reload=HotReloadController(controller);reload._compile_generation=reload.Generation
        reload.Stop();reload._Completed(BuildResult(True));controller.Runtime.ReloadScripts.assert_not_called()
        delete(controller)
    def test_watches_project_headers_and_ignores_unrelated_directory_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'Move.cpp';header=Path(folder)/'Move.h'
            source.write_text('COMPONENT(Move){}');header.write_text('struct Config{};')
            c=QObject();c.Runtime=Mock();c.Runtime.IsPlaying.return_value=True;c.Runtime.SceneScripts.return_value=[]
            c.ScriptAttachments=Mock();c.ScriptAttachments.UsedSources.return_value=[str(source)]
            c.Prefabs=Mock();c.Prefabs.NativeSources.return_value=[]
            c.ScriptCompiler=Mock();c.ScriptCompiler._ValidateIncludes.return_value={header}
            c.Window=Mock();c.Window.PreferenceValue.return_value=True
            reload=HotReloadController(c);reload.Track();self.assertIn(str(header),reload.Watcher.files())
            reload._Changed(str(source.parent));self.assertFalse(reload.Timer.isActive())
            header.write_text('struct Config{int Speed;};');reload._Changed(str(header));self.assertTrue(reload.Timer.isActive())
            reload.Stop();self.assertFalse(reload.Timer.isActive());self.assertFalse(reload.Watcher.files());delete(c)
    def test_native_bridge_keeps_live_state_and_rejects_incompatible_reload(self):
        from Editor.runtime import RuntimeService
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup);self.addCleanup(runtime.Release)
        root=Path(folder.name);assets=root/'Assets';assets.mkdir()
        project=root/'Game.bproject';project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Reload\nAssetDirectory: Assets\nStartupScene: ""\n',encoding='utf-8')
        self.assertTrue(runtime.LoadProject(project));entity=runtime.CreateEntity('Live');source=assets/'Walker.lua'
        source.write_text('local T={Speed=1}\nfunction T:OnCreate() self.Speed=9;local p=self.Entity:GetWorldTransform();p.Position.X=17;self.Entity:SetWorldTransform(p) end\nreturn T',encoding='utf-8')
        self.assertTrue(runtime.RefreshLuaAssets([str(source)]));binding=dict(module=str(source),entity=entity,type='Walker',properties={'Speed':1})
        self.assertTrue(runtime.SyncLuaScripts([dict(entity=entity,source=str(source),type='Walker',enabled=True,properties={'Speed':1})]))
        prefab=assets/'Walker.bprefab';self.assertTrue(runtime.SavePrefab(entity,prefab))
        self.assertTrue(runtime.ConfigureScripts([binding]));self.assertTrue(runtime.Play(),runtime.LastError())
        spawned=runtime.InstantiatePrefabPath(prefab);self.assertTrue(spawned);runtime.Tick()
        source.write_text('local T={Speed=1}\nfunction T:OnCreate() error("must not recreate") end\nfunction T:OnReload() assert(self.Speed==9);local p=self.Entity:GetWorldTransform();assert(p.Position.X==17);p.Position.X=23;self.Entity:SetWorldTransform(p) end\nreturn T',encoding='utf-8')
        self.assertTrue(runtime.RefreshLuaAssets([str(source)]));self.assertTrue(runtime.ReloadScripts([binding]),runtime.LastError())
        details=runtime.EntityDetails(entity);self.assertEqual(details['position'][0],23);self.assertEqual(runtime.EntityDetails(spawned)['position'][0],23)
        source.write_text('return {Speed="changed"}',encoding='utf-8');self.assertTrue(runtime.RefreshLuaAssets([str(source)]));self.assertFalse(runtime.ReloadScripts([binding]));self.assertEqual(runtime.EntityDetails(entity)['position'][0],23);self.assertTrue(runtime.IsPlaying());runtime.Stop()

if __name__=='__main__':unittest.main()
