"""Compact editor controls shown directly below the main menu."""

from __future__ import annotations

from enum import Enum

from PySide6.QtCore import QPoint, QRect, QSize, Qt, Signal
from PySide6.QtGui import (
    QAction, QActionGroup, QColor, QIcon, QPainter, QPaintEvent, QPixmap, QPolygon,
)
from PySide6.QtWidgets import QSizePolicy, QStyle, QToolBar, QToolButton, QWidget

from ...localization import LocalizationManager
from ...theme import Theme, ThemeManager
from ..gizmos import GizmoMode
from .menu_bar import EditorMenu


class PlayState(Enum):
    Stopped = "stopped"
    Playing = "playing"
    Paused = "paused"


class GizmoModeButton(QToolButton):
    """Theme-aware mode button with deterministic icon/text/arrow spacing."""

    _HorizontalPadding = 8
    _IconSize = 18
    _IconTextGap = 7
    _ArrowWidth = 10
    _ArrowHeight = 6
    _ArrowRightGap = 9
    _TextArrowGap = 8

    def __init__(self, themes: ThemeManager, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._themes = themes
        themes.ThemeChanged.connect(lambda _theme: self.update())

    def _IconRect(self) -> QRect:
        top = (self.height() - self._IconSize) // 2
        return QRect(self._HorizontalPadding, top, self._IconSize, self._IconSize)

    def _ArrowRect(self) -> QRect:
        left = self.width() - self._ArrowRightGap - self._ArrowWidth
        top = (self.height() - self._ArrowHeight) // 2
        return QRect(left, top, self._ArrowWidth, self._ArrowHeight)

    def _TextRect(self) -> QRect:
        left = self._IconRect().right() + 1 + self._IconTextGap
        right = self._ArrowRect().left() - self._TextArrowGap
        return QRect(left, 0, max(0, right - left), self.height())

    def sizeHint(self) -> QSize:
        width = (
            self._HorizontalPadding + self._IconSize + self._IconTextGap
            + self.fontMetrics().horizontalAdvance(self.text()) + self._TextArrowGap
            + self._ArrowWidth + self._ArrowRightGap
        )
        return QSize(max(125, width), 30)

    def paintEvent(self, event: QPaintEvent) -> None:
        del event
        theme = self._themes.GetTheme()
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        if self.isDown():
            background = theme.surface_pressed
        elif self.underMouse():
            background = theme.surface_hover
        else:
            background = theme.input_background
        painter.setPen(QColor(theme.border))
        painter.setBrush(QColor(background))
        painter.drawRoundedRect(self.rect().adjusted(0, 0, -1, -1), theme.radius, theme.radius)

        mode = QIcon.Mode.Normal if self.isEnabled() else QIcon.Mode.Disabled
        icon = self.icon().pixmap(QSize(self._IconSize, self._IconSize), mode)
        painter.drawPixmap(self._IconRect(), icon)
        painter.setPen(QColor(theme.text if self.isEnabled() else theme.text_disabled))
        text = self.fontMetrics().elidedText(
            self.text(), Qt.TextElideMode.ElideRight, self._TextRect().width()
        )
        painter.drawText(
            self._TextRect(), Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter, text
        )

        arrow = self._ArrowRect()
        center_x, center_y = arrow.center().x(), arrow.center().y()
        half = self._ArrowWidth // 2
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(theme.text_muted if self.isEnabled() else theme.text_disabled))
        painter.drawPolygon(QPolygon([
            QPoint(center_x - half, center_y - self._ArrowHeight // 2),
            QPoint(center_x + half, center_y - self._ArrowHeight // 2),
            QPoint(center_x, center_y + self._ArrowHeight // 2),
        ]))
        painter.end()


class EditorToolbar(QToolBar):
    PlayRequested = Signal()
    StopRequested = Signal()
    PauseRequested = Signal(bool)
    StepRequested = Signal()
    GizmoModeChanged = Signal(object)

    def __init__(self, localization: LocalizationManager, themes: ThemeManager,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setObjectName("EditorToolbar")
        self.setMovable(False)
        self.setFloatable(False)
        self.setAllowedAreas(Qt.ToolBarArea.TopToolBarArea)
        self.setIconSize(QSize(16, 16))
        self.setMaximumHeight(36)
        self._localization = localization
        self._themes = themes
        self._play_state = PlayState.Stopped

        self.PlayAction = QAction(self)
        self.StopAction = QAction(self)
        self.PauseAction = QAction(self)
        self.StepAction = QAction(self)
        self.PauseAction.setCheckable(True)

        self.ModeSelector = GizmoModeButton(themes, self)
        self.ModeSelector.setObjectName("GizmoModeSelector")
        self.ModeSelector.setPopupMode(QToolButton.ToolButtonPopupMode.InstantPopup)
        self.ModeSelector.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.ModeMenu = EditorMenu("", themes, self.ModeSelector)
        self.ModeSelector.setMenu(self.ModeMenu)
        self.ModeActions: dict[GizmoMode, QAction] = {}
        mode_group = QActionGroup(self)
        mode_group.setExclusive(True)
        for mode in GizmoMode:
            action = QAction(self.ModeMenu)
            action.setCheckable(True)
            action.setData(mode)
            action.triggered.connect(lambda checked=False, value=mode:
                                     checked and self.SetGizmoMode(value))
            mode_group.addAction(action)
            self.ModeMenu.addAction(action)
            self.ModeActions[mode] = action
        self.addWidget(self.ModeSelector)
        self.addSeparator()
        left_spacer = QWidget(self)
        left_spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.addWidget(left_spacer)
        self.addAction(self.PlayAction)
        self.addAction(self.StopAction)
        self.addAction(self.PauseAction)
        self.addAction(self.StepAction)
        right_spacer = QWidget(self)
        right_spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.addWidget(right_spacer)
        self.PlayAction.triggered.connect(self._Play)
        self.StopAction.triggered.connect(self._Stop)
        self.PauseAction.triggered.connect(self._Pause)
        self.StepAction.triggered.connect(self.StepRequested)
        localization.LocaleChanged.connect(lambda _: self._Retranslate())
        themes.ThemeChanged.connect(self._ThemeChanged)
        self._gizmo_mode = GizmoMode.Translate
        self._UpdateIcons()
        self._Retranslate()
        self.SetGizmoMode(GizmoMode.Translate)
        self.SetPlayState(PlayState.Stopped)

    def GetPlayState(self) -> PlayState:
        return self._play_state

    def SetPlayState(self, state: PlayState) -> None:
        if not isinstance(state, PlayState):
            raise TypeError("state must be a PlayState")
        self._play_state = state
        active = state is not PlayState.Stopped
        self.PlayAction.setEnabled(not active)
        self.StopAction.setEnabled(active)
        self.PauseAction.setEnabled(active)
        self.StepAction.setEnabled(active)
        self.PauseAction.blockSignals(True)
        self.PauseAction.setChecked(state is PlayState.Paused)
        self.PauseAction.blockSignals(False)

    def GetGizmoMode(self) -> GizmoMode:
        return self._gizmo_mode

    def SetGizmoMode(self, mode: GizmoMode) -> None:
        if mode not in self.ModeActions:
            raise ValueError(f"unsupported gizmo mode: {mode!r}")
        changed = mode is not self._gizmo_mode
        self._gizmo_mode = mode
        self.ModeActions[mode].setChecked(True)
        self.ModeSelector.setText(self.ModeActions[mode].text())
        self.ModeSelector.setIcon(self.ModeActions[mode].icon())
        if changed:
            self.GizmoModeChanged.emit(mode)

    def _Play(self) -> None:
        self.SetPlayState(PlayState.Playing)
        self.PlayRequested.emit()

    def _Stop(self) -> None:
        self.SetPlayState(PlayState.Stopped)
        self.StopRequested.emit()

    def _Pause(self, paused: bool) -> None:
        self.SetPlayState(PlayState.Paused if paused else PlayState.Playing)
        self.PauseRequested.emit(paused)

    def _ThemeChanged(self, _theme: Theme) -> None:
        self._UpdateIcons()
        self.SetGizmoMode(self._gizmo_mode)

    def _UpdateIcons(self) -> None:
        standards = {
            self.PlayAction: QStyle.StandardPixmap.SP_MediaPlay,
            self.StopAction: QStyle.StandardPixmap.SP_MediaStop,
            self.PauseAction: QStyle.StandardPixmap.SP_MediaPause,
            self.StepAction: QStyle.StandardPixmap.SP_MediaSkipForward,
        }
        for action, standard in standards.items():
            action.setIcon(self._ThemedStandardIcon(standard))
        theme_name = "light" if self._themes.GetTheme().background == "#d4d4d4" else "dark"
        names = {GizmoMode.Select:"sel_select.svg",GizmoMode.Translate:"sel_trans.svg",
                 GizmoMode.Rotate:"sel_rot.svg",GizmoMode.Scale:"sel_scale.svg"}
        resources=getattr(self.parentWidget(),"Resources",None)
        for mode,name in names.items():
            self.ModeActions[mode].setIcon(resources.Icon(f"icons/{theme_name}/{name}") if resources else QIcon())

    def _ThemedStandardIcon(self, standard: QStyle.StandardPixmap) -> QIcon:
        source = self.style().standardIcon(standard).pixmap(self.iconSize())
        theme = self._themes.GetTheme()

        def Tint(color: str) -> QPixmap:
            result = QPixmap(source.size())
            result.setDevicePixelRatio(source.devicePixelRatio())
            result.fill(Qt.GlobalColor.transparent)
            painter = QPainter(result)
            painter.drawPixmap(0, 0, source)
            painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn)
            painter.fillRect(result.rect(), QColor(color))
            painter.end()
            return result

        icon = QIcon(Tint(theme.text))
        icon.addPixmap(Tint(theme.text_disabled), QIcon.Mode.Disabled)
        return icon

    def _Retranslate(self) -> None:
        tr = self._localization.Translate
        title = tr("toolbar.title")
        self.setWindowTitle(title)
        self.toggleViewAction().setText(title)
        controls = (
            (self.PlayAction, "toolbar.play"),
            (self.StopAction, "toolbar.stop"),
            (self.PauseAction, "toolbar.pause"),
            (self.StepAction, "toolbar.step"),
        )
        for action, key in controls:
            action.setText(tr(key))
            action.setToolTip(tr(key))
        for mode, action in self.ModeActions.items():
            action.setText(tr(f"gizmo.{mode.value}"))
        self.ModeSelector.setText(self.ModeActions[self._gizmo_mode].text())
        self.ModeSelector.setToolTip(tr("gizmo.mode"))


__all__ = ["EditorToolbar", "GizmoModeButton", "PlayState"]
