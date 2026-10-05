"""Guard UI hot paths against expensive full-scene snapshots and rebuilds."""
import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
import tempfile
from pathlib import Path
from time import monotonic
from unittest.mock import Mock, patch
from PySide6.QtCore import QEvent
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor
from Editor.resources import ResourceManager

class EditorPerformanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):self.Window=Editor(settings={},settings_saver=lambda _:None)
    def tearDown(self):
        self.Window.Controller.IsDirty=False;self.Window.Controller._dirty_scenes.clear()
        self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

    def test_icon_requests_resolve_once_and_share_cached_svg(self):
        resources=ResourceManager()
        with patch.object(resources,"Path",wraps=resources.Path) as resolve:
            icons=[resources.Icon("icons/dark/obj.svg") for _ in range(100)]
        self.assertEqual(resolve.call_count,1)
        self.assertEqual(len({icon.cacheKey() for icon in icons}),1)

    def test_panel_presentation_changes_rebuild_once(self):
        docking=self.Window.Docking
        with patch.object(docking,"_rebuild_views",wraps=docking._rebuild_views) as rebuild:
            self.Window._UpdatePanelPresentation()
        self.assertEqual(rebuild.call_count,1)

    def test_selection_and_gizmo_ticks_do_not_serialize_whole_scene(self):
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge unavailable")
        window=self.Window;entity=window.Runtime.CreateEntity("Performance target")
        with patch.object(window.Runtime,"LoadedScenes",side_effect=AssertionError("selection serialized all scenes")):
            window.Controller.SelectEntity(entity)
        self.assertEqual(window.Controller.SelectedEntity,entity)
        window.Controller._material_check=monotonic();window.Controller._stats_started=0
        with patch.object(window.Runtime,"Entities",side_effect=AssertionError("tick serialized all entities")),patch.object(window.Runtime,"EntityDetails",side_effect=AssertionError("gizmo serialized component data")),patch.object(window.Runtime,"Tick",return_value=True):
            window.Controller._Tick()
        self.assertIsNotNone(window.Scene.Surface._selection)

    def test_default_theme_still_populates_hierarchy(self):
        self.assertGreater(self.Window.Hierarchy.Tree.topLevelItemCount(),0)

    def test_asset_selection_does_not_rescan_import_database(self):
        with tempfile.TemporaryDirectory() as directory:
            asset=Path(directory)/"mesh.obj";asset.write_text("v 0 0 0\n")
            with patch.object(self.Window.Runtime,"RefreshAssets",side_effect=AssertionError("selection rescanned/imported assets")):
                self.Window.Controller.SelectAsset(asset)
            self.assertIn("asset",self.Window.Properties._sections)

    def test_transform_undo_preserves_hierarchy_without_reloading_scene(self):
        window=self.Window;runtime=window.Runtime
        if not runtime.IsAvailable():self.skipTest("native editor bridge unavailable")
        parent=runtime.CreateEntity("Parent");child=runtime.CreateEntity("Child",parent)
        runtime.SetTransform(parent,(4,0,0),(0,0,0,1),(1,1,1));runtime.SetTransform(child,(5,0,0),(0,0,0,1),(1,1,1))
        controller=window.Controller;controller.SelectEntities([parent,child]);controller.History.Clear()
        before=runtime.CaptureTransforms([parent,child])
        with patch.object(runtime,"CaptureScene",side_effect=AssertionError("transform captured entire scene")),patch.object(runtime,"RestoreScene",side_effect=AssertionError("undo reloaded model scene")),patch.object(controller,"RefreshHierarchy",side_effect=AssertionError("transform undo rebuilt hierarchy")):
            controller.History.BeginTransforms("Move selection",[parent,child])
            runtime.SetTransform(parent,(8,0,0),(0,0,0,1),(1,1,1));runtime.SetTransform(child,(10,0,0),(0,0,0,1),(1,1,1))
            after=runtime.CaptureTransforms([parent,child]);self.assertTrue(controller.History.Commit())
            controller.Undo();self.assertEqual(runtime.CaptureTransforms([parent,child]),before)
            controller.Redo();self.assertEqual(runtime.CaptureTransforms([parent,child]),after)
        self.assertEqual(runtime.EntityDetails(child)["parent"],parent)

    def test_unchanged_editor_flags_do_not_resubmit_native_mesh_visibility(self):
        if not self.Window.Runtime.IsAvailable():self.skipTest("native editor bridge unavailable")
        runtime=self.Window.Runtime;scenes=runtime.LoadedScenes()
        host=Mock(wraps=runtime._host)
        with patch.object(runtime,"_host",host):
            runtime.SyncEditorEntityState(scenes);runtime.SyncEditorEntityState(scenes)
            self.assertEqual(host.set_editor_entity_state.call_count,0)
