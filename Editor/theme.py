"""Application-wide editor theme tokens and Qt styling."""

from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication, QWidget

CHECK_ICON_PATH = ":/docking/check.svg"


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
    #ViewportPlaceholderLabel {{ color: {t.text_muted}; }}
    #ComponentSection {{ background: {t.surface_alt}; border: 1px solid {t.border};
                         border-radius: {t.radius}px; }}
    #ComponentSection QToolButton {{ background: transparent; border: 0;
                                     padding: {t.spacing}px; text-align: left; }}
    QPushButton {{ background: {t.surface_alt}; color: {t.text};
                   border: 1px solid {t.border}; border-radius: {t.radius}px;
                   padding: {t.spacing}px {t.spacing * 2}px; }}
    QPushButton:hover {{ border-color: {t.accent}; }}
    #DockEmpty {{ background: {t.background}; border: 1px dashed {t.border}; }}
    #DockEmptyLabel {{ color: {t.text_muted}; font-size: 13px; }}
    QTabWidget::pane {{ border: 1px solid {t.border}; background: {t.surface}; top: -1px; }}
    QTabBar {{ background: {t.background}; border: 0; }}
    QTabBar::tab {{ background: {t.tab_inactive}; color: {t.text_muted}; border: 1px solid {t.border};
                   border-top: 0;
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
             padding: {t.spacing / 2}px 0; border-radius: 2px; }}
    QMenu::item {{ padding: 4px {t.spacing * 6 + 8}px 4px {t.spacing * 4 + 8}px; }}
    QMenu::item:selected {{ background: {t.accent}; color: {t.text}; }}
    QMenu::item:disabled {{ color: {t.text_muted}; }}
    QMenu::icon {{ left: {t.spacing * 2}px; }}
    QMenu::indicator {{ width: 14px; height: 14px; left: {t.spacing * 2}px; }}
    QMenu::indicator:checked {{ image: url({CHECK_ICON_PATH}); }}
    QMenu::right-arrow {{ subcontrol-position: right center; right: {t.spacing * 2}px; }}
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
        self.SetTheme(self._theme)

    def GetTheme(self) -> Theme:
        return self._theme

    def SetTheme(self, theme: Theme) -> None:
        if not isinstance(theme, Theme):
            raise TypeError("theme must be a Theme instance")
        self._theme = theme
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
