"""Shared custom menu bar for every BAZZALT editor window."""

from __future__ import annotations

from PySide6.QtCore import QRect, Qt, Signal
from PySide6.QtGui import QAction, QActionGroup, QColor, QKeySequence, QPainter, QPaintEvent
from PySide6.QtWidgets import QApplication, QMenu, QMenuBar, QWidget

from ...localization import LocalizationManager
from ...theme import Theme, ThemeManager
from ..docking import DockingSystem


class EditorMenu(QMenu):
    """Menu with a dedicated passive-colored shortcut column."""

    def __init__(self, title: str, themes: ThemeManager, parent: QWidget | None = None) -> None:
        super().__init__(title, parent)
        self._themes = themes
        themes.ThemeChanged.connect(self._ThemeChanged)

    def _ThemeChanged(self, _: Theme) -> None:
        self.updateGeometry()
        self.update()

    def AddSubMenu(self, title: str) -> "EditorMenu":
        menu = EditorMenu(title, self._themes, self)
        super().addMenu(menu)
        return menu

    def sizeHint(self):  # type: ignore[no-untyped-def]
        """Use one label and one shortcut column instead of Qt's oversized hint."""
        size = super().sizeHint()
        metrics = self.fontMetrics()
        actions = [action for action in self.actions() if action.isVisible()]
        label_width = max((metrics.horizontalAdvance(action.text().replace("&", ""))
                           for action in actions), default=0)
        shortcut_width = max((metrics.horizontalAdvance(action.shortcut().toString(
            QKeySequence.SequenceFormat.NativeText)) for action in actions
            if not action.shortcut().isEmpty()), default=0)
        spacing = self._themes.GetTheme().spacing
        column_gap = spacing * 2 if shortcut_width else 0
        # Covers the check/icon gutter, both outer margins, and submenu arrow.
        chrome = spacing * 4 + 16
        size.setWidth(max(120, label_width + shortcut_width + column_gap + chrome))
        return size

    def paintEvent(self, event: QPaintEvent) -> None:
        # Preserve native/QSS menu layout, selection, indicators, separators,
        # submenu arrows, disabled states, and platform behavior.
        super().paintEvent(event)
        theme = self._themes.GetTheme()
        shortcuts = [
            action.shortcut().toString(QKeySequence.SequenceFormat.NativeText)
            for action in self.actions()
            if action.isVisible() and not action.shortcut().isEmpty()
        ]
        if not shortcuts:
            return
        shortcut_width = max(self.fontMetrics().horizontalAdvance(value)
                             for value in shortcuts)
        spacing = theme.spacing
        column_left = max(0, self.width() - shortcut_width - spacing * 5)
        painter = QPainter(self)
        for action in self.actions():
            if (not action.isVisible() or action.isSeparator() or
                    action.menu() is not None or action.shortcut().isEmpty()):
                continue
            shortcut = action.shortcut().toString(QKeySequence.SequenceFormat.NativeText)
            action_rect = self.actionGeometry(action)
            cover = QRect(column_left, action_rect.top(),
                          self.width() - column_left - spacing, action_rect.height())
            background = theme.accent if action is self.activeAction() else theme.surface
            painter.fillRect(cover, QColor(background))
            painter.setPen(QColor(theme.text_muted))
            painter.drawText(cover.adjusted(0, 0, -spacing * 2, 0),
                Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, shortcut)
        painter.end()


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
        localization: LocalizationManager,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.setObjectName("EditorMenuBar")
        self.setNativeMenuBar(False)
        self._theme_manager = theme_manager
        self._localization = localization
        self._docking: DockingSystem | None = None

        self.FileMenu = self._AddTopLevelMenu("")
        self.EditMenu = self._AddTopLevelMenu("")
        self.ViewMenu = self._AddTopLevelMenu("")
        self.WindowMenu = self._AddTopLevelMenu("")
        self.HelpMenu = self._AddTopLevelMenu("")

        self._BuildFileMenu()
        self._BuildEditMenu()
        self._BuildViewMenu()
        self._BuildWindowMenu()
        self._BuildHelpMenu()
        self._localization.LocaleChanged.connect(lambda _: self._Retranslate())
        self._Retranslate()

    def SetDockingSystem(self, docking: DockingSystem | None) -> None:
        """Bind the panel list used by the Window menu."""
        self._docking = docking

    def AddMenu(self, title: str) -> QMenu:
        """Create an additional consistently styled top-level menu."""
        return self._AddTopLevelMenu(title)

    def _AddTopLevelMenu(self, title: str) -> EditorMenu:
        menu = EditorMenu(title, self._theme_manager, self)
        self.addMenu(menu)
        return menu

    def _BuildFileMenu(self) -> None:
        self.NewProjectAction = action = self.FileMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.New)
        action.triggered.connect(self.NewProjectRequested)

        self.OpenProjectAction = action = self.FileMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.Open)
        action.triggered.connect(self.OpenProjectRequested)

        self.FileMenu.addSeparator()
        self.SaveProjectAction = action = self.FileMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.Save)
        action.triggered.connect(self.SaveProjectRequested)

        self.SaveProjectAsAction = action = self.FileMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.SaveAs)
        action.triggered.connect(self.SaveProjectAsRequested)

        self.FileMenu.addSeparator()
        self.ExitAction = action = self.FileMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.Quit)
        action.triggered.connect(self._Quit)

    def _BuildEditMenu(self) -> None:
        self.UndoAction = action = self.EditMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.Undo)
        action.triggered.connect(self.UndoRequested)

        self.RedoAction = action = self.EditMenu.addAction("")
        action.setShortcut(QKeySequence.StandardKey.Redo)
        action.triggered.connect(self.RedoRequested)

    def _BuildViewMenu(self) -> None:
        self.ThemeMenu = theme_menu = self.ViewMenu.AddSubMenu("")
        group = QActionGroup(self)
        group.setExclusive(True)

        self.DarkThemeAction = dark = theme_menu.addAction("")
        self.LightThemeAction = light = theme_menu.addAction("")
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
        self.AboutAction = about = self.HelpMenu.addAction("")
        about.setEnabled(False)

    def _RefreshWindowMenu(self) -> None:
        self.WindowMenu.clear()
        reset = self.WindowMenu.addAction(self._localization.Translate("action.reset_workspace"))
        reset.triggered.connect(self.ResetWorkspaceRequested)

        if self._docking is None or not self._docking.panels():
            self.WindowMenu.addSeparator()
            empty = self.WindowMenu.addAction(self._localization.Translate("action.no_panels"))
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

    def _Retranslate(self) -> None:
        tr = self._localization.Translate
        self.FileMenu.setTitle(tr("menu.file"))
        self.EditMenu.setTitle(tr("menu.edit"))
        self.ViewMenu.setTitle(tr("menu.view"))
        self.WindowMenu.setTitle(tr("menu.window"))
        self.HelpMenu.setTitle(tr("menu.help"))
        self.NewProjectAction.setText(tr("action.new_project"))
        self.OpenProjectAction.setText(tr("action.open_project"))
        self.SaveProjectAction.setText(tr("action.save_project"))
        self.SaveProjectAsAction.setText(tr("action.save_project_as"))
        self.ExitAction.setText(tr("action.exit"))
        self.UndoAction.setText(tr("action.undo"))
        self.RedoAction.setText(tr("action.redo"))
        self.ThemeMenu.setTitle(tr("action.theme"))
        self.DarkThemeAction.setText(tr("action.theme_dark"))
        self.LightThemeAction.setText(tr("action.theme_light"))
        self.AboutAction.setText(tr("action.about"))

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


__all__ = ["EditorMenu", "EditorMenuBar"]
