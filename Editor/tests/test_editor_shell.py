from __future__ import annotations

import os
from pathlib import Path
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QLabel

from Editor.gui.application import Editor
from Editor.gui.panels import ConsoleLevel
from Editor.gui.widgets import (
    AssetPickerInput,
    EnumInput,
    FieldState,
    FloatInput,
    MultiSelectInput,
    RangeInput,
    Vec3Input,
    PlayState,
)
from Editor.gui.gizmos import GizmoMode
from Editor.gui.widgets import EditorMenu
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import BuildStyleSheet, Theme, ThemeManager


class EditorShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.Application = QApplication.instance() or QApplication([])

    def setUp(self) -> None:
        self.Themes = ThemeManager(self.Application)
        self.Window = Editor(self.Themes)

    def tearDown(self) -> None:
        self.Window.close()

    def test_shared_theme_updates_docking(self) -> None:
        self.Themes.SetTheme(Theme.light())
        self.assertEqual(self.Window.Docking.theme.background, Theme.light().background)

    def test_shortcuts_use_custom_menu_rendering(self) -> None:
        self.assertIsInstance(self.Window.MenuBar.FileMenu, EditorMenu)
        save = next(action for action in self.Window.MenuBar.FileMenu.actions()
                    if action.text() == "Save Project")
        # Native shortcut layout stays enabled for correct sizing; EditorMenu's
        # paint pass strips that text and draws exactly one muted shortcut.
        self.assertTrue(save.isShortcutVisibleInContextMenu())
        self.assertFalse(save.shortcut().isEmpty())
        menu = self.Window.MenuBar.FileMenu
        menu.resize(menu.sizeHint())
        self.assertLess(menu.sizeHint().width(), 500)
        self.assertTrue(all(menu.actionGeometry(action).right() <= menu.width()
                            for action in menu.actions()))

    def test_nested_menus_fit_their_native_action_rows(self) -> None:
        view = self.Window.MenuBar.ViewMenu
        theme = view.actions()[0].menu()
        self.assertIsNotNone(theme)
        for menu in (view, theme):
            menu.resize(menu.sizeHint())
            self.assertTrue(all(menu.actionGeometry(action).right() <= menu.width()
                                for action in menu.actions()))

    def test_resources_and_localization_are_shared_services(self) -> None:
        self.assertIsInstance(self.Window.Resources, ResourceManager)
        self.assertIsInstance(self.Window.Localization, LocalizationManager)
        self.assertEqual(self.Window.Localization.Translate("action.save_project"), "Save Project")
        self.assertTrue(self.Window.Resources.Resolve("locales/en.json").is_file())
        with self.assertRaises(ValueError):
            self.Window.Resources.Resolve(Path("..") / "outside.svg")

    def test_layout_restore_rejects_oversized_input(self) -> None:
        self.assertFalse(self.Window.Docking.restore_layout(b" " * (2 * 1024 * 1024 + 1)))

    def test_window_menu_lists_shared_dock_panels(self) -> None:
        self.Window.Docking.add_panel(
            QLabel("Inspector"), "Inspector", panel_id="inspector"
        )
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        labels = [action.text() for action in self.Window.MenuBar.WindowMenu.actions()]
        self.assertIn("Inspector", labels)

    def test_window_menu_checks_every_open_tab_not_only_active_tab(self) -> None:
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        actions = {action.text(): action for action in self.Window.MenuBar.WindowMenu.actions()}
        self.assertTrue(actions["Scene"].isChecked())
        self.assertTrue(actions["Output"].isChecked())
        self.assertTrue(actions["Scene"].icon().isNull())
        self.Window.Docking.close_panel("output")
        self.Window.MenuBar.WindowMenu.aboutToShow.emit()
        actions = {action.text(): action for action in self.Window.MenuBar.WindowMenu.actions()}
        self.assertFalse(actions["Output"].isChecked())

    def test_typed_component_field_widgets(self) -> None:
        number = FloatInput(value=2.5)
        number.SetFieldState(FieldState.Modified)
        self.assertEqual(number.GetValue(), 2.5)
        self.assertEqual(number.GetFieldState(), FieldState.Modified)

        vector = Vec3Input((1.0, 2.0, 3.0))
        self.assertEqual(vector.GetValue(), (1.0, 2.0, 3.0))
        vector.SetValue((4.0, 5.0, 6.0))
        self.assertEqual(vector.GetValue(), (4.0, 5.0, 6.0))

        ranged = RangeInput(-1.0, 1.0, 0.25)
        ranged.SetValue(9.0)
        self.assertEqual(ranged.GetValue(), 1.0)

    def test_enum_multiselect_and_picker_widgets(self) -> None:
        enum = EnumInput()
        enum.SetOptions((("Perspective", "perspective"), ("Orthographic", "ortho")))
        self.assertTrue(enum.SetValue("ortho"))
        self.assertEqual(enum.GetValue(), "ortho")

        choices = MultiSelectInput()
        choices.SetOptions((("Static", 1), ("Visible", 2), ("Locked", 3)))
        choices.SetValues((1, 3))
        self.assertEqual(choices.GetValues(), (1, 3))

        picker = AssetPickerInput("Select asset")
        picker.SetValue("asset-uuid", "Player Mesh")
        self.assertEqual(picker.GetValue(), "asset-uuid")
        self.assertTrue(picker.ClearButton.isEnabled())
        picker.Clear()
        self.assertIsNone(picker.GetValue())

    def test_editor_uses_custom_menu_bar(self) -> None:
        self.assertIs(self.Window.menuBar(), self.Window.MenuBar)
        self.assertFalse(self.Window.MenuBar.isNativeMenuBar())

    def test_native_runtime_populates_hierarchy_and_inspector(self) -> None:
        if not self.Window.Runtime.IsAvailable():
            self.skipTest("native editor bridge is not built")
        entity_id = self.Window.Runtime.CreateEntity("Bridge Entity")
        self.assertEqual(self.Window.Hierarchy.Tree.topLevelItemCount(), 1)
        self.Window.Controller.SelectEntity(entity_id)
        self.assertIn("transform", self.Window.Properties._sections)
        details = self.Window.Runtime.EntityDetails(entity_id)
        self.assertEqual(details["name"], "Bridge Entity")
        self.assertTrue(self.Window.Runtime.SetTransform(
            entity_id, (3.0, 2.0, 1.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0)))
        self.assertEqual(self.Window.Runtime.EntityDetails(entity_id)["position"], (3.0, 2.0, 1.0))

    def test_editor_toolbar_transport_and_modes(self) -> None:
        toolbar = self.Window.Toolbar
        self.assertFalse(toolbar.isMovable())
        self.assertEqual(toolbar.GetPlayState(), PlayState.Stopped)
        self.assertFalse(toolbar.StopAction.isEnabled())
        toolbar.PlayAction.trigger()
        self.assertEqual(toolbar.GetPlayState(), PlayState.Playing)
        self.assertTrue(toolbar.StopAction.isEnabled())
        toolbar.PauseAction.trigger()
        self.assertEqual(toolbar.GetPlayState(), PlayState.Paused)
        toolbar.SetGizmoMode(GizmoMode.Rotate)
        self.assertEqual(toolbar.GetGizmoMode(), GizmoMode.Rotate)
        self.assertTrue(all(not action.icon().isNull()
                            for action in toolbar.ModeActions.values()))
        self.assertEqual(toolbar.toggleViewAction().text(), "Editor Toolbar")
        self.assertIsInstance(toolbar.ModeMenu, EditorMenu)
        toolbar.ModeSelector.resize(toolbar.ModeSelector.sizeHint())
        icon_rect = toolbar.ModeSelector._IconRect()
        text_rect = toolbar.ModeSelector._TextRect()
        arrow_rect = toolbar.ModeSelector._ArrowRect()
        self.assertGreaterEqual(text_rect.left() - icon_rect.right() - 1, 7)
        self.assertGreaterEqual(arrow_rect.left() - text_rect.right(), 8)
        self.assertGreaterEqual(toolbar.ModeSelector.width() - arrow_rect.right() - 1, 9)
        self.assertEqual(arrow_rect.center().y(), toolbar.ModeSelector.rect().center().y())

    def test_toolbar_icons_refresh_for_theme(self) -> None:
        before = self.Window.Toolbar.PlayAction.icon().cacheKey()
        self.Themes.SetTheme(Theme.light())
        after = self.Window.Toolbar.PlayAction.icon().cacheKey()
        self.assertNotEqual(before, after)

    def test_menu_icons_and_dock_tabs_have_clean_edge_spacing(self) -> None:
        style = BuildStyleSheet(Theme.dark())
        self.assertIn("QMenu::icon { left: 12px; }", style)
        self.assertIn("border-top: 0;", style)

    def test_default_workspace_registers_all_builtin_panels(self) -> None:
        self.assertEqual(
            {panel.panel_id for panel in self.Window.Docking.panels()},
            {"console", "output", "scene", "hierarchy", "properties", "asset_browser"},
        )
        self.assertTrue(all(not panel.icon.isNull() for panel in self.Window.Docking.panels()))

    def test_console_api_filters_and_clears_messages(self) -> None:
        self.Window.Console.AddMessage("built", ConsoleLevel.Info)
        self.Window.Console.AddMessage("failed", ConsoleLevel.Error)
        self.Window.Console.SetLevelVisible(ConsoleLevel.Info, False)
        self.assertNotIn("built", self.Window.Console.View.toPlainText())
        self.assertIn("failed", self.Window.Console.View.toPlainText())
        self.Window.Console.Clear()
        self.assertEqual(self.Window.Console.GetMessages(), ())

    def test_properties_sections_and_sticky_add_button(self) -> None:
        section = self.Window.Properties.AddComponentSection("transform", "Transform")
        section.SetExpanded(False)
        self.assertFalse(section.Body.isVisible())
        self.assertEqual(self.Window.Properties.layout().itemAt(
            self.Window.Properties.layout().count() - 1
        ).widget(), self.Window.Properties.AddComponentButton)

    def test_asset_browser_hides_sidecar_metadata(self) -> None:
        root = Path(__file__).parent / "fixtures" / "assets"
        self.Window.AssetBrowser.SetProjectRoot(root)
        names = [self.Window.AssetBrowser.Browser.item(i).text()
                 for i in range(self.Window.AssetBrowser.Browser.count())]
        self.assertIn("Textures", names)
        self.assertIn("player.png", names)
        self.assertNotIn("player.png.meta", names)

    def test_reset_workspace_restores_closed_panel(self) -> None:
        self.Window.Docking.close_panel("console")
        self.Window._ResetWorkspace()
        placed = str(self.Window.Docking.save_layout())
        self.assertIn("console", placed)


if __name__ == "__main__":
    unittest.main()
