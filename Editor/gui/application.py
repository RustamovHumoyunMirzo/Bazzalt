"""Top-level BAZZALT editor window."""

from __future__ import annotations

from PySide6.QtWidgets import QApplication, QMainWindow

from ..localization import LocalizationManager
from ..resources import ResourceManager
from ..theme import ThemeManager
from .docking import DockingSystem
from .widgets import EditorMenuBar


class Editor(QMainWindow):
    def __init__(
        self,
        theme_manager: ThemeManager | None = None,
        resources: ResourceManager | None = None,
        localization: LocalizationManager | None = None,
    ) -> None:
        super().__init__()
        self.setObjectName("BazzaltEditor")
        self.resize(1024, 720)

        application = QApplication.instance()
        if application is None:
            raise RuntimeError("Create QApplication before constructing Editor")
        self.ThemeManager = theme_manager or ThemeManager(application)
        self.Resources = resources or ResourceManager()
        self.Localization = localization or LocalizationManager(self.Resources)
        self.setWindowTitle(self.Localization.Translate("app.title"))
        self.Localization.LocaleChanged.connect(
            lambda _: self.setWindowTitle(self.Localization.Translate("app.title"))
        )
        self.Docking = DockingSystem(
            self.ThemeManager.GetTheme(), self, localization=self.Localization
        )
        self.MenuBar = EditorMenuBar(self.ThemeManager, self.Localization, self)
        self.MenuBar.SetDockingSystem(self.Docking)
        self.MenuBar.ResetWorkspaceRequested.connect(self.Docking.reset)
        self.ThemeManager.ThemeChanged.connect(self.Docking.set_theme)

        self.setMenuBar(self.MenuBar)
        self.setCentralWidget(self.Docking)

        # Lower-case aliases preserve the original prototype's attributes.
        self.docking = self.Docking


__all__ = ["Editor"]
