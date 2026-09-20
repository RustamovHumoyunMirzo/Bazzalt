"""Top-level BAZZALT editor window."""

from __future__ import annotations

from PySide6.QtWidgets import QApplication, QMainWindow

from ..theme import ThemeManager
from .docking import DockingSystem
from .widgets import EditorMenuBar


class Editor(QMainWindow):
    def __init__(self, theme_manager: ThemeManager | None = None) -> None:
        super().__init__()
        self.setObjectName("BazzaltEditor")
        self.setWindowTitle("BAZZALT")
        self.resize(1024, 720)

        application = QApplication.instance()
        if application is None:
            raise RuntimeError("Create QApplication before constructing Editor")
        self.ThemeManager = theme_manager or ThemeManager(application)
        self.Docking = DockingSystem(self.ThemeManager.GetTheme(), self)
        self.MenuBar = EditorMenuBar(self.ThemeManager, self)
        self.MenuBar.SetDockingSystem(self.Docking)
        self.MenuBar.ResetWorkspaceRequested.connect(self.Docking.reset)
        self.ThemeManager.ThemeChanged.connect(self.Docking.set_theme)

        self.setMenuBar(self.MenuBar)
        self.setCentralWidget(self.Docking)

        # Lower-case aliases preserve the original prototype's attributes.
        self.docking = self.Docking


__all__ = ["Editor"]
