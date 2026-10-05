import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import json
import struct
import tempfile
import unittest
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
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
        themes.SetTheme(Theme.dark());dark=button.icon().cacheKey();themes.SetTheme(Theme.light());self.assertNotEqual(dark,button.icon().cacheKey())
        section.deleteLater();button.deleteLater()

    def test_actual_obj_preview_and_async_browser_update(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.obj";path.write_text("v -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n",encoding="utf-8")
            image=RenderModelThumbnail(path);self.assertFalse(image.isNull());self.assertEqual(image.width(),96)
            self.assertTrue(any(image.pixelColor(x,y).alpha() for x in range(96) for y in range(96)))
            panel=AssetBrowserPanel(self.Locale,self.Resources);panel.SetProjectRoot(folder);item=panel.Browser.item(0);old=item.icon().cacheKey();item.setSelected(True)
            for _ in range(100):
                QTest.qWait(10)
                if item.icon().cacheKey()!=old:break
            self.assertNotEqual(item.icon().cacheKey(),old);self.assertTrue(item.isSelected());self.assertEqual(panel.CurrentFolder(),Path(folder))
            path.write_text("broken",encoding="utf-8");self.assertTrue(RenderModelThumbnail(path).isNull())
            panel.deleteLater()

    def test_glb_node_transforms_and_invalid_geometry(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"mesh.glb";positions=struct.pack("<9f",-1,0,0,1,0,0,0,2,0)
            document={"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0,"translation":[100,0,0]}],"scenes":[{"nodes":[0]}]}
            data=json.dumps(document).encode();data+=b" "*((-len(data))%4);chunks=struct.pack("<II",len(data),0x4e4f534a)+data+struct.pack("<II",36,0x004e4942)+positions
            path.write_bytes(struct.pack("<III",0x46546c67,2,12+len(chunks))+chunks)
            self.assertFalse(RenderModelThumbnail(path).isNull())
            path.write_bytes(b"invalid")
            with self.assertRaises(ValueError):RenderModelThumbnail(path)
