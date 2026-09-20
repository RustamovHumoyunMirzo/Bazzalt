from __future__ import annotations

import os
from pathlib import Path
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QLabel

from Editor.gui.application import Editor
from Editor.gui.widgets import EditorMenu
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.theme import Theme, ThemeManager


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
        self.assertLess(self.Window.MenuBar.FileMenu.sizeHint().width(), 400)

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

    def test_editor_uses_custom_menu_bar(self) -> None:
        self.assertIs(self.Window.menuBar(), self.Window.MenuBar)
        self.assertFalse(self.Window.MenuBar.isNativeMenuBar())


if __name__ == "__main__":
    unittest.main()
