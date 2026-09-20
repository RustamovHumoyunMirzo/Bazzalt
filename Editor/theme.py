"""Application-wide editor theme tokens and Qt styling."""

from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QColor, QPainter, QPalette
from PySide6.QtWidgets import (
    QApplication,
    QProxyStyle,
    QStyle,
    QStyleFactory,
    QStyleOption,
    QStyleOptionMenuItem,
    QWidget,
)

CHECK_ICON_PATH = ":/docking/check.svg"


class _EditorStyle(QProxyStyle):
    """Draw menu shortcut columns with the theme's passive text color."""

    def __init__(self, base_style_name: str, muted_color: str, spacing: int) -> None:
        super().__init__(QStyleFactory.create(base_style_name))
        self.MutedColor = QColor(muted_color)
        self.Spacing = spacing

    def SetTheme(self, theme: "Theme") -> None:
        self.MutedColor = QColor(theme.text_muted)
        self.Spacing = theme.spacing

    def drawControl(
        self,
        element: QStyle.ControlElement,
        option: QStyleOption,
        painter: QPainter,
        widget: QWidget | None = None,
    ) -> None:
        if (
            element != QStyle.ControlElement.CE_MenuItem
            or not isinstance(option, QStyleOptionMenuItem)
            or "\t" not in option.text
            or not (option.state & QStyle.StateFlag.State_Enabled)
        ):
            super().drawControl(element, option, painter, widget)
            return

        _, shortcut = option.text.rsplit("\t", 1)
        passive = QStyleOptionMenuItem(option)
        passive.palette = QPalette(option.palette)
        for group in (QPalette.ColorGroup.Active, QPalette.ColorGroup.Inactive):
            passive.palette.setColor(group, QPalette.ColorRole.Text, self.MutedColor)
            passive.palette.setColor(
                group, QPalette.ColorRole.WindowText, self.MutedColor
            )

        # Paint the complete row passively, then repaint the label/icon/check
        # area with the original palette. Qt keeps ownership of menu metrics.
        super().drawControl(element, passive, painter, widget)
        shortcut_width = option.fontMetrics.horizontalAdvance(shortcut)
        label_right = option.rect.right() - shortcut_width - self.Spacing * 3
        painter.save()
        painter.setClipRect(
            option.rect.adjusted(0, 0, label_right - option.rect.right(), 0)
        )
        super().drawControl(element, option, painter, widget)
        painter.restore()


@dataclass(slots=True)
class Theme:
    """Color, spacing, and sizing tokens shared by every editor widget."""

    background: str = "#181818"
    surface: str = "#282828"
    surface_alt: str = "#212121"
    border: str = "#181818"
    accent: str = "#476b8f"
    text: str = "#dbdbdb"
    text_muted: str = "#8c8c8c"
    tab_active: str = "#282828"
    tab_inactive: str = "#1e1e1e"
    overlay_opacity: int = 204
    radius: int = 4
    spacing: int = 6
    splitter_width: int = 3
    minimum_panel_size: int = 120

    @classmethod
    def dark(cls) -> "Theme":
        return cls()

    @classmethod
    def light(cls) -> "Theme":
        return cls(
            background="#d4d4d4",
            surface="#ffffff",
            surface_alt="#e5e5e5",
            border="#cccccc",
            accent="#7bb5f0",
            text="#1f1f1f",
            text_muted="#6e6e6e",
            tab_active="#ffffff",
            tab_inactive="#dbdbdb",
        )


def BuildPalette(theme: Theme) -> QPalette:
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(theme.background))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(theme.text))
    palette.setColor(QPalette.ColorRole.Base, QColor(theme.surface))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor(theme.surface_alt))
    palette.setColor(QPalette.ColorRole.Text, QColor(theme.text))
    palette.setColor(QPalette.ColorRole.Button, QColor(theme.surface_alt))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor(theme.text))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(theme.accent))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor(theme.text))
    palette.setColor(
        QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, QColor(theme.text_muted)
    )
    palette.setColor(
        QPalette.ColorGroup.Disabled,
        QPalette.ColorRole.ButtonText,
        QColor(theme.text_muted),
    )
    return palette


def BuildStyleSheet(theme: Theme) -> str:
    """Return the single style sheet used by the application and floating UI."""
    t = theme
    return f"""
    QWidget {{ color: {t.text}; selection-background-color: {t.accent}; }}
    QMainWindow, #DockingSystem, _FloatingWindow {{ background: {t.background}; color: {t.text}; }}
    #DockPanel, #DockGroup {{ background: {t.surface}; border: 0; }}
    #DockPanel QAbstractItemView, #DockPanel QLineEdit, #DockPanel QTextEdit,
    #DockPanel QPlainTextEdit {{ background: {t.surface}; color: {t.text};
                               border: 1px solid {t.border}; selection-background-color: {t.accent}; }}
    QLineEdit {{ padding: {t.spacing}px; border: 1px solid {t.border}; border-radius: {t.radius}px; }}
    QLabel {{ color: {t.text}; }}
    #DockEmpty {{ background: {t.background}; border: 1px dashed {t.border}; }}
    #DockEmptyLabel {{ color: {t.text_muted}; font-size: 13px; }}
    QTabWidget::pane {{ border: 1px solid {t.border}; background: {t.surface}; top: -1px; }}
    QTabBar::tab {{ background: {t.tab_inactive}; color: {t.text_muted}; border: 1px solid {t.border};
                   border-top-left-radius: {t.radius}px; border-top-right-radius: {t.radius}px;
                   padding: {t.spacing + 2}px {t.spacing * 2 + 2}px; min-width: 55px; margin-right: 1px; }}
    QTabBar::tab:selected {{ background: {t.tab_active}; color: {t.text}; border-bottom-color: {t.tab_active}; }}
    QTabBar::tab:hover:!selected {{ background: {t.surface_alt}; color: {t.text}; }}
    QSplitter::handle {{ background: {t.background}; }}
    QSplitter::handle:hover {{ background: {t.accent}; }}
    QMenuBar#EditorMenuBar {{ background: {t.surface_alt}; color: {t.text};
                              border-bottom: 1px solid {t.border}; padding: 4px {t.spacing}px; }}
    QMenuBar#EditorMenuBar::item {{ background: transparent; padding: 3px 10px;
                                    border-radius: {t.radius}px; }}
    QMenuBar#EditorMenuBar::item:selected, QMenuBar#EditorMenuBar::item:pressed {{
        background: {t.surface}; color: {t.text}; }}
    QMenu {{ background: {t.surface}; color: {t.text}; border: 1px solid {t.border};
             padding: {t.spacing / 2}px 0; }}
    QMenu::item {{ padding: 4px {t.spacing * 5}px 4px {t.spacing * 4 + 8}px; }}
    QMenu::item:selected {{ background: {t.accent}; color: {t.text}; }}
    QMenu::item:disabled {{ color: {t.text_muted}; }}
    QMenu::indicator {{ width: 14px; height: 14px; left: {t.spacing * 2}px; }}
    QMenu::indicator:checked {{ image: url({CHECK_ICON_PATH}); }}
    QMenu::separator {{ height: 1px; background: {t.border}; margin: 3px {t.spacing}px; }}
    QToolTip {{ background: {t.surface}; color: {t.text}; border: 1px solid {t.border}; }}
    QScrollBar {{ background: {t.background}; }}
    """


def ApplyWidgetTheme(widget: QWidget, theme: Theme) -> None:
    widget.setPalette(BuildPalette(theme))
    widget.setStyleSheet(BuildStyleSheet(theme))
    style = widget.style()
    style.unpolish(widget)
    style.polish(widget)
    widget.update()


class ThemeManager(QObject):
    """Owns and applies the one active theme for the entire editor process."""

    ThemeChanged = Signal(object)

    def __init__(self, application: QApplication, theme: Theme | None = None) -> None:
        super().__init__(application)
        self._application = application
        self._theme = theme or Theme.dark()
        base_style_name = application.style().objectName() or "Fusion"
        self._style = _EditorStyle(
            base_style_name, self._theme.text_muted, self._theme.spacing
        )
        self._application.setStyle(self._style)
        self.SetTheme(self._theme)

    def GetTheme(self) -> Theme:
        return self._theme

    def SetTheme(self, theme: Theme) -> None:
        if not isinstance(theme, Theme):
            raise TypeError("theme must be a Theme instance")
        self._theme = theme
        self._style.SetTheme(theme)
        self._application.setPalette(BuildPalette(theme))
        self._application.setStyleSheet(BuildStyleSheet(theme))
        self.ThemeChanged.emit(theme)


__all__ = [
    "ApplyWidgetTheme",
    "BuildPalette",
    "BuildStyleSheet",
    "Theme",
    "ThemeManager",
]
