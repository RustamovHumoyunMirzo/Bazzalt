from __future__ import annotations

import os
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QLabel

from Editor.gui.application import Editor
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
        self.assertEqual(
            self.Themes._style.MutedColor.name(), Theme.light().text_muted
        )

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
