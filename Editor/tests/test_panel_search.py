import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
import unittest
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QWidget
from Editor.gui.panels.hierarchy import HierarchyPanel
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import Theme, ThemeManager


class PanelSearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App=QApplication.instance() or QApplication([])

    def setUp(self):
        self.Resources=ResourceManager();self.Locale=LocalizationManager(self.Resources)

    def test_hierarchy_keeps_ancestors_and_restores_expansion_without_selection_changes(self):
        panel=HierarchyPanel(self.Locale)
        scene=panel.AddItem("Main Scene","scene",kind="scene")
        group=panel.AddItem("Objects","group",scene)
        camera=panel.AddItem("Main Camera","camera",group)
        light=panel.AddItem("Sun Light","light",scene)
        scene.setExpanded(True);group.setExpanded(False);panel.SetSelectedData(["camera"])
        changes=[];panel.SelectionChanged.connect(changes.append)
        panel.Search.setText("CAMERA")
        self.assertFalse(scene.isHidden());self.assertFalse(group.isHidden());self.assertFalse(camera.isHidden())
        self.assertTrue(light.isHidden());self.assertTrue(group.isExpanded())
        self.assertEqual(panel.GetSelectedData(),["camera"]);self.assertEqual(changes,[])
        new=panel.AddItem("Other Light","other",scene);self.App.processEvents()
        self.assertTrue(new.isHidden())
        panel.Search.clear()
        self.assertFalse(light.isHidden());self.assertFalse(new.isHidden());self.assertFalse(group.isExpanded())
        panel.close()

    def test_asset_search_is_project_wide_keeps_folder_and_hides_metadata(self):
        panel=AssetBrowserPanel(self.Locale,self.Resources)
        root=(Path(__file__).parent/"fixtures/assets").resolve();panel.SetProjectRoot(root)
        panel.Search.setText("TEXTURES .keep");panel._PopulateBrowser()
        self.assertEqual(panel.CurrentFolder(),root)
        self.assertEqual(panel.Browser.count(),1)
        item=panel.Browser.item(0)
        self.assertEqual(Path(item.data(Qt.ItemDataRole.UserRole)),root/"Textures/.keep")
        self.assertIn("Textures",item.toolTip())
        self.assertTrue(panel.Browser.mimeData([item]).hasFormat("application/x-bazzalt-asset"))
        panel.Refresh();self.assertEqual(panel.Browser.count(),1)
        panel.Search.setText("player");panel._PopulateBrowser()
        self.assertEqual(panel.Browser.count(),1)
        self.assertEqual(panel.Browser.item(0).text(),"player")
        panel.Search.clear();panel._PopulateBrowser()
        self.assertEqual(panel.CurrentFolder(),root);self.assertGreater(panel.Browser.count(),1)
        panel.close()

    def test_refresh_reveals_created_asset_even_when_search_does_not_match(self):
        panel=AssetBrowserPanel(self.Locale,self.Resources)
        root=(Path(__file__).parent/"fixtures/assets").resolve();panel.SetProjectRoot(root)
        panel.Search.setText("missing");panel._PopulateBrowser()
        self.assertEqual(panel.Browser.count(),0)
        panel.Refresh(root/"Textures/.keep")
        self.assertEqual(panel.Search.text(),"")
        self.assertEqual(panel.CurrentFolder(),root/"Textures")
        self.assertEqual(Path(panel.Browser.currentItem().data(Qt.ItemDataRole.UserRole)),root/"Textures/.keep")
        panel.close()

    def test_shared_input_height_is_compact_in_both_themes(self):
        manager=ThemeManager(self.App)
        for theme in (Theme.dark(),Theme.light()):
            manager.SetTheme(theme)
            for cls in (QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox):
                field=cls();field.ensurePolished()
                self.assertLessEqual(field.maximumHeight(),22)
                field.close()
            inspector=QWidget();inspector.setObjectName("PropertiesPanel")
            field=QLineEdit(inspector);field.ensurePolished()
            self.assertLessEqual(field.maximumHeight(),20)
            inspector.close()
