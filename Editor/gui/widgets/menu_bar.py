"""Shared custom menu bar for every BAZZALT editor window."""

from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtGui import QAction, QActionGroup, QKeySequence
from PySide6.QtWidgets import QApplication, QMenu, QMenuBar, QWidget

from ...theme import Theme, ThemeManager
from ..docking import DockingSystem


class EditorMenuBar(QMenuBar):
    """Editor commands, workspace controls, and global theme selection."""

    NewProjectRequested = Signal()
    OpenProjectRequested = Signal()
    SaveProjectRequested = Signal()
    SaveProjectAsRequested = Signal()
    UndoRequested = Signal()
    RedoRequested = Signal()
    ResetWorkspaceRequested = Signal()

    def __init__(
        self,
        theme_manager: ThemeManager,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("EditorMenuBar")
        self.setNativeMenuBar(False)
        self._theme_manager = theme_manager
        self._docking: DockingSystem | None = None

        self.FileMenu = self.addMenu("&File")
        self.EditMenu = self.addMenu("&Edit")
        self.ViewMenu = self.addMenu("&View")
        self.WindowMenu = self.addMenu("&Window")
        self.HelpMenu = self.addMenu("&Help")

        self._BuildFileMenu()
        self._BuildEditMenu()
        self._BuildViewMenu()
        self._BuildWindowMenu()
        self._BuildHelpMenu()

    def SetDockingSystem(self, docking: DockingSystem | None) -> None:
        """Bind the panel list used by the Window menu."""
        self._docking = docking

    def AddMenu(self, title: str) -> QMenu:
        """Create an additional consistently styled top-level menu."""
        return self.addMenu(title)

    def _BuildFileMenu(self) -> None:
        action = self.FileMenu.addAction("New Project")
        action.setShortcut(QKeySequence.StandardKey.New)
        action.triggered.connect(self.NewProjectRequested)

        action = self.FileMenu.addAction("Open Project…")
        action.setShortcut(QKeySequence.StandardKey.Open)
        action.triggered.connect(self.OpenProjectRequested)

        self.FileMenu.addSeparator()
        action = self.FileMenu.addAction("Save Project")
        action.setShortcut(QKeySequence.StandardKey.Save)
        action.triggered.connect(self.SaveProjectRequested)

        action = self.FileMenu.addAction("Save Project As…")
        action.setShortcut(QKeySequence.StandardKey.SaveAs)
        action.triggered.connect(self.SaveProjectAsRequested)

        self.FileMenu.addSeparator()
        action = self.FileMenu.addAction("Exit")
        action.setShortcut(QKeySequence.StandardKey.Quit)
        action.triggered.connect(self._Quit)

    def _BuildEditMenu(self) -> None:
        action = self.EditMenu.addAction("Undo")
        action.setShortcut(QKeySequence.StandardKey.Undo)
        action.triggered.connect(self.UndoRequested)

        action = self.EditMenu.addAction("Redo")
        action.setShortcut(QKeySequence.StandardKey.Redo)
        action.triggered.connect(self.RedoRequested)

    def _BuildViewMenu(self) -> None:
        theme_menu = self.ViewMenu.addMenu("Theme")
        group = QActionGroup(self)
        group.setExclusive(True)

        dark = theme_menu.addAction("Dark")
        light = theme_menu.addAction("Light")
        for action in (dark, light):
            action.setCheckable(True)
            group.addAction(action)
        dark.setChecked(True)
        dark.triggered.connect(lambda checked: checked and self._theme_manager.SetTheme(Theme.dark()))
        light.triggered.connect(lambda checked: checked and self._theme_manager.SetTheme(Theme.light()))

        def SyncTheme(theme: Theme) -> None:
            dark.setChecked(theme.background == Theme.dark().background)
            light.setChecked(theme.background == Theme.light().background)

        self._theme_manager.ThemeChanged.connect(SyncTheme)
        SyncTheme(self._theme_manager.GetTheme())

    def _BuildWindowMenu(self) -> None:
        self.WindowMenu.aboutToShow.connect(self._RefreshWindowMenu)

    def _BuildHelpMenu(self) -> None:
        about = self.HelpMenu.addAction("About BAZZALT")
        about.setEnabled(False)

    def _RefreshWindowMenu(self) -> None:
        self.WindowMenu.clear()
        reset = self.WindowMenu.addAction("Reset Workspace")
        reset.triggered.connect(self.ResetWorkspaceRequested)

        if self._docking is None or not self._docking.panels():
            self.WindowMenu.addSeparator()
            empty = self.WindowMenu.addAction("No registered panels")
            empty.setEnabled(False)
            return

        self.WindowMenu.addSeparator()
        for panel in self._docking.panels():
            action = QAction(panel.icon, panel.title, self.WindowMenu)
            action.setCheckable(True)
            action.setChecked(panel.isVisible())
            action.setEnabled(panel.isVisible() or panel.closable)
            action.triggered.connect(
                lambda checked, panel_id=panel.panel_id: self._SetPanelVisible(panel_id, checked)
            )
            self.WindowMenu.addAction(action)

    def _SetPanelVisible(self, panel_id: str, visible: bool) -> None:
        if self._docking is None:
            return
        if visible:
            self._docking.dock(panel_id, "center")
        else:
            self._docking.close_panel(panel_id)

    @staticmethod
    def _Quit() -> None:
        application = QApplication.instance()
        if application is not None:
            application.quit()


__all__ = ["EditorMenuBar"]
