import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import json
import struct
import tempfile
import unittest
from time import monotonic, sleep
from threading import Event
from unittest.mock import patch
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication
from Editor.gui.panels.hierarchy import HierarchyPanel
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.gui.panels.properties import ComponentSection
from Editor.gui.widgets.options_button import OptionsButton
from Editor.gui.widgets.menu_bar import EditorMenuBar
from Editor.gui.preferences import MergePreferences
from Editor.localization import LocalizationManager
from Editor.model_thumbnails import RenderModelThumbnail
from Editor.resources import ResourceManager
from Editor.theme import ThemeManager, Theme

class BrowserHierarchyHelpersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):self.Resources=ResourceManager();self.Locale=LocalizationManager(self.Resources)

    def _WaitFor(self,predicate,timeout=5):
        deadline=monotonic()+timeout
        while not predicate() and monotonic()<deadline:
            self.App.processEvents()
            # Yield Python's GIL as well as pumping Qt. Repeated native qWait
            # calls can starve a Python QRunnable on some PySide/CI builds.
            sleep(.01)
        self.App.processEvents()
        return predicate()

    def test_object_menu_has_only_modes_selection_placement_snapping(self):
        menu=EditorMenuBar(ThemeManager(self.App),self.Locale)
        self.assertEqual(set(menu.ObjectMenus),{"selection","placement","snapping"})
        menu.deleteLater()

    def test_collapsed_default_explicit_expansion_and_helpers_survive_rebuild(self):
        panel=HierarchyPanel(self.Locale);self.assertFalse(MergePreferences({})["general"]["expand_new_hierarchy_items"])
        def populate():
            panel.Clear();scene=panel.AddItem("Scene","scene",kind="scene");parent=panel.AddItem("Parent","parent",scene);panel.AddItem("Child","child",parent)
            panel.ApplyExpansionState({"scene":scene,"parent":parent});return scene,parent
        scene,parent=populate();self.assertTrue(scene.isExpanded());self.assertFalse(parent.isExpanded())
        parent.setExpanded(True);scene,parent=populate();self.assertTrue(parent.isExpanded())
        panel.SetAllExpanded(False);scene,parent=populate();self.assertFalse(scene.isExpanded());self.assertFalse(parent.isExpanded())
        panel.SetSelectedData(["child"]);panel.RevealSelected();self.assertTrue(scene.isExpanded());self.assertTrue(parent.isExpanded())
        panel._collapsed_ids.clear();panel._expanded_ids.clear();panel.ExpandNewItems=True;scene,parent=populate();self.assertTrue(parent.isExpanded())
        panel.deleteLater()

    def test_options_icons_follow_palette_and_component_uses_shared_button(self):
        themes=ThemeManager(self.App);button=OptionsButton();section=ComponentSection("Test",localization=self.Locale)
        self.assertFalse(button.icon().isNull());self.assertIsInstance(section.findChild(OptionsButton,"ComponentOptionsButton"),OptionsButton)
        self.assertTrue(button.property("editorOptionsButton"));self.assertTrue(button.autoRaise())
        self.assertTrue(section.findChild(OptionsButton,"ComponentOptionsButton").property("editorOptionsButton"))
        themes.SetTheme(Theme.dark());dark=button.icon().cacheKey();themes.SetTheme(Theme.light());self.assertNotEqual(dark,button.icon().cacheKey())
        section.deleteLater();button.deleteLater()

    def test_actual_obj_preview_and_async_browser_update(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n",encoding="utf-8")
            image=RenderModelThumbnail(path);self.assertFalse(image.isNull());self.assertEqual(image.width(),96)
            self.assertTrue(any(image.pixelColor(x,y).alpha() for x in range(96) for y in range(96)))
            panel=AssetBrowserPanel(self.Locale,self.Resources);completed=[]
            panel._thumbnails.Ready.connect(lambda name,image:completed.append((name,image)))
            try:
                panel.SetProjectRoot(folder);item=panel.Browser.item(0);old=item.icon().cacheKey();item.setSelected(True)
                finished=self._WaitFor(lambda:bool(completed))
                self.assertTrue(finished,f"Thumbnail completion timeout: {panel._thumbnails.WorkerDiagnostics()}")
                # SetProjectRoot resolves aliases (including Windows RUNNER~1
                # short names); verify file identity rather than path spelling.
                self.assertTrue(Path(completed[0][0]).samefile(path));self.assertFalse(completed[0][1].isNull(),"Thumbnail generation returned an empty image")
                self.assertNotEqual(item.icon().cacheKey(),old,"Worker completed but browser did not apply the thumbnail")
                self.assertTrue(item.isSelected());self.assertTrue(panel.CurrentFolder().samefile(folder))
                path.write_text("broken",encoding="utf-8");self.assertTrue(RenderModelThumbnail(path).isNull())
            finally:
                panel._thumbnails._pool.waitForDone(5000);panel.deleteLater()

    def test_delayed_worker_delivers_completion_on_gui_thread(self):
        from Editor.model_thumbnails import ModelThumbnailCache
        from PySide6.QtCore import QThread
        gate=Event();started=Event();completed=[]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n",encoding="utf-8")
            cache=ModelThumbnailCache()
            cache.Ready.connect(lambda name,image:completed.append((name,image,QThread.currentThread())))
            def delayed(path):
                started.set()
                if not gate.wait(5):raise TimeoutError("test worker gate not released")
                return RenderModelThumbnail(path)
            try:
                with patch("Editor.model_thumbnails.RenderModelThumbnail",side_effect=delayed):
                    self.assertIsNone(cache.Request(path));self.assertTrue(self._WaitFor(started.is_set));self.assertFalse(completed)
                    gate.set();self.assertTrue(self._WaitFor(lambda:bool(completed)))
                self.assertFalse(completed[0][1].isNull());self.assertEqual(completed[0][2],self.App.thread())
                self.assertIsNotNone(cache.Request(path))
            finally:gate.set();cache._pool.waitForDone(5000);cache.deleteLater()

    def test_static_preview_is_reused_across_cache_instances(self):
        from Editor.model_thumbnails import ModelThumbnailCache
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n",encoding="utf-8")
            completed=[];cache=ModelThumbnailCache(cache_root=Path(folder)/"cache")
            cache.Ready.connect(lambda name,image:completed.append(image));cache.Request(path)
            self.assertTrue(self._WaitFor(lambda:bool(completed)));self.assertFalse(completed[0].isNull());cache._pool.waitForDone(5000)
            second=ModelThumbnailCache(cache_root=Path(folder)/"cache");loaded=[];second.Ready.connect(lambda name,image:loaded.append(image))
            with patch("Editor.model_thumbnails.RenderModelThumbnail",side_effect=AssertionError("cached thumbnail regenerated")):
                second.Request(path);self.assertTrue(self._WaitFor(lambda:bool(loaded)))
            self.assertFalse(loaded[0].isNull());second._pool.waitForDone(5000);cache.deleteLater();second.deleteLater()

    def test_worker_shutdown_cancels_queue_and_suppresses_delivery(self):
        from Editor.model_thumbnails import ModelThumbnailCache
        gate=Event();started=Event();completed=[]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("broken",encoding="utf-8")
            cache=ModelThumbnailCache(cache_root=Path(folder)/"cache")
            cache.Ready.connect(lambda *args:completed.append(args))
            def delayed(path):
                started.set();gate.wait(5)
                return RenderModelThumbnail(path)
            try:
                with patch("Editor.model_thumbnails.RenderModelThumbnail",side_effect=delayed):
                    cache.Request(path);self.assertTrue(self._WaitFor(started.is_set))
                    cache._pool.Close();gate.set()
                    self.assertTrue(cache._pool.waitForDone(5000));self.App.processEvents()
                self.assertFalse(completed);self.assertIsNone(cache.Request(path))
            finally:gate.set();cache._pool.Close();cache.deleteLater()

    def test_completed_future_waits_for_gui_collection(self):
        from Editor.model_thumbnails import ModelThumbnailCache
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n",encoding="utf-8")
            cache=ModelThumbnailCache(cache_root=Path(folder)/"cache");completed=[]
            cache.Ready.connect(lambda *args:completed.append(args))
            try:
                cache.Request(path)
                self.assertTrue(cache._pool.waitForDone(5000))
                self.assertFalse(completed) # GUI was deliberately not pumped.
                self.assertIn("done",cache.WorkerDiagnostics()["jobs"].values())
                self.assertTrue(self._WaitFor(lambda:bool(completed)))
                self.assertFalse(cache._completion_timer.isActive());self.assertFalse(cache._pending)
            finally:cache._pool.Close();cache.deleteLater()

    def test_failed_future_is_reported_and_releases_pending_slot(self):
        from Editor.model_thumbnails import ModelThumbnailCache
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("broken",encoding="utf-8")
            cache=ModelThumbnailCache(cache_root=Path(folder)/"cache");completed=[]
            cache.Ready.connect(lambda name,image:completed.append(image))
            try:
                with patch("Editor.model_thumbnails._Work.run",side_effect=RuntimeError("worker failure")):
                    cache.Request(path);self.assertTrue(self._WaitFor(lambda:bool(completed)))
                self.assertTrue(completed[0].isNull());self.assertFalse(cache._pending)
                self.assertIn("worker failure",cache.WorkerDiagnostics()["errors"][str(path)])
            finally:cache._pool.Close();cache.deleteLater()

    def test_glb_node_transforms_and_invalid_geometry(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.glb";positions=struct.pack("<9f",-1,0,0,1,0,0,0,2,0)
            document={"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[100,0,0]}],"scenes":[{"nodes":[0]}]}
            data=json.dumps(document).encode();data+=b" "*((-len(data))%4);chunks=struct.pack("<II",len(data),0x4e4f534a)+data+struct.pack("<II",36,0x004e4942)+positions
            path.write_bytes(struct.pack("<III",0x46546c67,2,12+len(chunks))+chunks)
            self.assertFalse(RenderModelThumbnail(path).isNull())
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):RenderModelThumbnail(path)
