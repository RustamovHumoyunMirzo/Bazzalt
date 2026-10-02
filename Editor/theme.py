"""Application-wide editor theme tokens and Qt styling."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication, QWidget

ASSET_ROOT = Path(__file__).resolve().parent / "assets" / "icons"


@dataclass(slots=True)
class Theme:
    """Color, spacing, and sizing tokens shared by every editor widget."""

    background: str = "#181818"
    surface: str = "#282828"
    surface_alt: str = "#212121"
    surface_hover: str = "#333333"
    surface_pressed: str = "#1b1b1b"
    input_background: str = "#202020"
    border: str = "#181818"
    border_focus: str = "#6f9bc7"
    accent: str = "#476b8f"
    selection: str = "#476b8f"
    text: str = "#dbdbdb"
    text_muted: str = "#8c8c8c"
    text_disabled: str = "#5f5f5f"
    success: str = "#62b47a"
    warning: str = "#d7a84b"
    error: str = "#d96565"
    modified: str = "#5c91c7"
    axis_x: str = "#d45b5b"
    axis_y: str = "#68a85c"
    axis_z: str = "#5686cf"
    axis_w: str = "#b176c2"
    tab_active: str = "#282828"
    tab_inactive: str = "#1e1e1e"
    overlay_opacity: int = 204
    radius: int = 4
    spacing: int = 6
    splitter_width: int = 3
    minimum_panel_size: int = 120
    control_height: int = 20
    row_height: int = 24

    @classmethod
    def dark(cls) -> "Theme":
        return cls()

    @classmethod
    def light(cls) -> "Theme":
        return cls(
            background="#d4d4d4",
            surface="#ffffff",
            surface_alt="#e5e5e5",
            surface_hover="#eeeeee",
            surface_pressed="#d8d8d8",
            input_background="#ffffff",
            border="#cccccc",
            border_focus="#397ebd",
            accent="#9bc9f5",
            selection="#9bc9f5",
            text="#1f1f1f",
            text_muted="#6e6e6e",
            text_disabled="#a0a0a0",
            success="#2f7d43",
            warning="#9a6812",
            error="#b63f3f",
            modified="#3479ba",
            axis_x="#b84242",
            axis_y="#438237",
            axis_z="#386db8",
            axis_w="#8a50a0",
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
    palette.setColor(QPalette.ColorRole.Highlight, QColor(theme.selection))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor(theme.text))
    palette.setColor(
        QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, QColor(theme.text_disabled)
    )
    palette.setColor(
        QPalette.ColorGroup.Disabled,
        QPalette.ColorRole.ButtonText,
        QColor(theme.text_disabled),
    )
    return palette


def BuildStyleSheet(theme: Theme) -> str:
    """Return the single style sheet used by the application and floating UI."""
    t = theme
    icon_theme = "light" if t.background.lower() == "#d4d4d4" else "dark"
    check_icon = (ASSET_ROOT / icon_theme / "check_selected.svg").as_posix()
    branch_closed = (ASSET_ROOT / icon_theme / "tree_closed.svg").as_posix()
    branch_open = (ASSET_ROOT / icon_theme / "tree_open.svg").as_posix()
    combo_arrow = branch_open
    return f"""
    QWidget {{ color: {t.text}; selection-background-color: {t.selection}; font-size: 12px; }}
    QWidget:disabled {{ color: {t.text_disabled}; }}
    QMainWindow, #DockingSystem, _FloatingWindow {{ background: {t.background}; color: {t.text}; }}
    #DockPanel, #DockGroup {{ background: {t.surface}; border: 0; }}
    QScrollArea, QAbstractScrollArea {{ background: {t.surface}; border: 0; }}
    QAbstractItemView {{ background: {t.surface}; color: {t.text}; border: 0;
        outline: 0; alternate-background-color: {t.surface_alt}; selection-background-color: {t.selection}; }}
    QAbstractItemView::item {{ min-height: {t.row_height}px; padding: 1px 5px; border: 0; }}
    QAbstractItemView::item:hover {{ background: {t.surface_hover}; }}
    QAbstractItemView::item:selected {{ background: {t.selection}; color: {t.text}; }}
    QListWidget#ConsoleMessageList {{ background: {t.input_background}; border: 1px solid {t.border}; }}
    QListWidget#ConsoleMessageList::item {{ padding: 3px 7px; border-bottom: 1px solid {t.border}; }}
    QListWidget#ConsoleMessageList::item:hover {{ background: {t.surface_hover}; }}
    QListWidget#ConsoleMessageList::item:selected {{ background: {t.selection}; }}
    QDialog#PreferencesDialog {{ background: {t.background}; }}
    QListWidget#PreferencesSections {{ background: {t.surface_alt}; border: 1px solid {t.border}; border-radius: {t.radius}px; padding: 4px; }}
    QListWidget#PreferencesSections::item {{ padding: 7px 9px; margin: 1px; border-radius: {t.radius}px; }}
    QListWidget#PreferencesSections::item:selected {{ background: {t.selection}; color: {t.text}; }}
    QLabel#PreferencesHint {{ color: {t.text_muted}; padding-top: 8px; }}
    QToolButton#ConsoleLevelFilter {{ background: transparent; border: 0; padding: 3px 25px 3px 6px; }}
    QToolButton#ConsoleLevelFilter:hover {{ background: {t.surface_hover}; }}
    QToolButton#ConsoleLevelFilter::menu-indicator {{ image: url({combo_arrow});
        width: 12px; height: 12px; subcontrol-origin: padding;
        subcontrol-position: right center; right: 6px; }}
    QTreeView::branch {{ background: transparent; width: 14px; }}
    QTreeView::branch:has-children:closed {{ image: url({branch_closed}); }}
    QTreeView::branch:has-children:open {{ image: url({branch_open}); }}
    QTextEdit, QPlainTextEdit {{ background: {t.input_background}; color: {t.text};
                                border: 1px solid {t.border}; border-radius: {t.radius}px; }}
    QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {{ background: {t.input_background};
        color: {t.text}; min-height: {t.control_height}px; max-height: {t.control_height}px; padding: 0 6px;
        border: 1px solid {t.border}; border-radius: {t.radius}px; }}
    QComboBox {{ padding-right: 26px; }}
    QComboBox::drop-down {{ subcontrol-origin: padding; subcontrol-position: top right;
        width: 24px; border: 0; border-left: 1px solid {t.border}; }}
    QComboBox::down-arrow {{ image: url({combo_arrow}); width: 12px; height: 12px; }}
    QComboBox QAbstractItemView {{ border: 1px solid {t.border}; padding: 3px; }}
    QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {{
        border-color: {t.border_focus}; }}
    QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
    QComboBox:disabled {{ background: {t.surface_alt}; color: {t.text_disabled}; }}
    QWidget[fieldState="modified"] {{ border-color: {t.modified}; }}
    QWidget[fieldState="warning"] {{ border-color: {t.warning}; }}
    QWidget[fieldState="error"] {{ border-color: {t.error}; }}
    QLabel {{ color: {t.text}; }}
    QCheckBox {{ spacing: 7px; min-height: {t.control_height}px; }}
    QCheckBox::indicator {{ width: 14px; height: 14px; background: {t.input_background};
        border: 1px solid {t.border}; border-radius: 3px; }}
    QCheckBox::indicator:hover {{ border-color: {t.border_focus}; }}
    QCheckBox::indicator:checked {{ background: {t.accent}; image: url({check_icon}); }}
    #ViewportPlaceholderLabel {{ color: {t.text_muted}; }}
    #SceneViewControls {{ background: {t.surface_alt}; border-bottom: 1px solid {t.border}; }}
    #SceneViewControls {{ background: {t.surface_alt}; border-bottom: 1px solid {t.border}; }}
    #SceneViewControls QComboBox {{ min-height: 20px; max-height: 20px; min-width: 68px; font-size: 11px; padding-left: 7px; }}
    #SceneViewControls QToolButton {{ min-height: 22px; max-height: 22px; padding: 0 8px; background: {t.input_background}; border: 1px solid {t.border}; border-radius: 3px; }}
    #SceneViewControls QToolButton:hover {{ background: {t.surface_hover}; }}
    #SceneViewControls QToolButton#SceneToolChip {{ min-width: 25px; max-width: 25px; padding: 0; }}
    #SceneViewControls QToolButton#SceneToolChip:checked {{ background: {t.selection}; border-color: {t.border_focus}; }}
    #SceneViewControls QComboBox#ScenePivotMode {{ min-width: 72px; max-width: 84px; }}
    #SceneViewControls QCheckBox, #SceneViewControls QLabel {{ min-height: 20px; max-height: 20px; font-size: 11px; }}
    #ComponentSection {{ background: {t.surface}; border: 1px solid {t.border}; border-radius: 2px; }}
    #ComponentHeader {{ background: {t.surface}; border: 0; border-bottom: 1px solid {t.border};
                        border-radius: 0; min-height: 21px; max-height: 21px;
                        padding: 0 5px; text-align: left; font-size: 11px; font-weight: 600; }}
    #ComponentHeader:hover {{ background: {t.surface_hover}; }}
    #ComponentHeaderRow {{ background: {t.surface}; border-bottom: 1px solid {t.border}; }}
    #ComponentOptionsButton {{ min-width: 24px; max-width: 24px; min-height: 21px; max-height: 21px;
                               border: 0; border-radius: 0; color: {t.text_muted}; font-size: 16px; font-weight: 700; }}
    #ComponentOptionsButton:hover {{ color: {t.text}; background: {t.surface_hover}; }}
    #PropertiesPanel #InspectorFieldLabel {{ color: {t.text_muted}; font-size: 11px; }}
    #PropertiesPanel QLineEdit, #PropertiesPanel QSpinBox, #PropertiesPanel QDoubleSpinBox,
    #PropertiesPanel QComboBox, #PropertiesPanel QPushButton {{ min-height: 18px; max-height: 18px;
        padding-top: 0; padding-bottom: 0; font-size: 11px; border-radius: 2px; }}
    #PropertiesPanel #VectorAxisLabel {{ color: {t.text_muted}; font-size: 10px; font-weight: 400; }}
    QPushButton {{ background: {t.surface_alt}; color: {t.text};
                   border: 1px solid {t.border}; border-radius: {t.radius}px;
                   min-height: {t.control_height}px; padding: 0 {t.spacing * 2}px; }}
    QPushButton:focus, QToolButton:focus {{ border: 1px solid {t.border_focus}; }}
    QPushButton:disabled {{ background: {t.surface_alt}; color: {t.text_disabled}; }}
    QPushButton:hover, QToolButton:hover {{ background: {t.surface_hover}; }}
    QPushButton:pressed, QToolButton:pressed {{ background: {t.surface_pressed}; }}
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
    QSlider::groove:horizontal {{ height: 3px; background: {t.border}; border-radius: 1px; }}
    QSlider::sub-page:horizontal {{ background: {t.accent}; border-radius: 1px; }}
    QSlider::handle:horizontal {{ width: 12px; margin: -5px 0; background: {t.text_muted};
                                 border: 1px solid {t.background}; border-radius: 6px; }}
    QSlider::handle:horizontal:hover {{ background: {t.text}; }}
    QProgressBar {{ min-height: 5px; max-height: 5px; background: {t.input_background};
                    border: 0; border-radius: 2px; text-align: center; color: transparent; }}
    QProgressBar::chunk {{ background: {t.accent}; border-radius: 2px; }}
    QMenuBar#EditorMenuBar {{ background: {t.surface_alt}; color: {t.text};
                              border-bottom: 1px solid {t.border}; padding: 4px {t.spacing}px; }}
    QMenuBar#EditorMenuBar::item {{ background: transparent; padding: 3px 10px;
                                    border-radius: {t.radius}px; }}
    QMenuBar#EditorMenuBar::item:selected, QMenuBar#EditorMenuBar::item:pressed {{
        background: {t.surface}; color: {t.text}; }}
    QToolBar#EditorToolbar {{ background: {t.surface_alt}; border: 0;
                              border-bottom: 1px solid {t.border}; padding: 2px {t.spacing}px;
                              spacing: 3px; }}
    QToolBar#EditorToolbar QToolButton {{ background: transparent; border: 0;
                                         border-radius: {t.radius}px; padding: 4px; }}
    QToolBar#EditorToolbar QToolButton:hover {{ background: {t.surface_hover}; }}
    QToolBar#EditorToolbar QToolButton:pressed,
    QToolBar#EditorToolbar QToolButton:checked {{ background: {t.selection}; }}
    QToolBar#EditorToolbar::separator {{ background: {t.border}; width: 1px;
                                        margin: 4px {t.spacing}px; }}
    #GizmoModeSelector {{ min-width: 125px; border: 0; padding: 0; }}
    #GizmoModeSelector::menu-indicator {{ image: none; width: 0; height: 0; }}
    QMenu {{ background: {t.surface}; color: {t.text}; border: 1px solid {t.border};
             padding: {t.spacing / 2}px 0; border-radius: 2px; }}
    QMenu::item {{ padding: 4px {t.spacing * 6 + 8}px 4px {t.spacing * 4 + 8}px; }}
    QMenu::item:selected {{ background: {t.selection}; color: {t.text}; }}
    QMenu::item:disabled {{ color: {t.text_disabled}; }}
    QMenu::icon {{ left: {t.spacing * 2}px; }}
    QMenu::indicator {{ width: 14px; height: 14px; left: {t.spacing * 2}px; }}
    QMenu::indicator:checked {{ image: url({check_icon}); }}
    QMenu::right-arrow {{ subcontrol-position: right center; right: {t.spacing * 2}px; }}
    QMenu::separator {{ height: 1px; background: {t.border}; margin: 3px {t.spacing}px; }}
    QToolTip {{ background: {t.surface}; color: {t.text}; border: 1px solid {t.border}; }}
    QScrollBar:vertical {{ background: {t.surface}; width: 10px; margin: 0; }}
    QScrollBar:horizontal {{ background: {t.surface}; height: 10px; margin: 0; }}
    QScrollBar::handle {{ background: {t.text_disabled}; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }}
    QScrollBar::handle:hover {{ background: {t.text_muted}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
    QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}
    #PickerButton, #PickerClearButton {{ min-width: {t.control_height}px; max-width: {t.control_height}px; padding: 0; }}
    #AddComponentButton {{ margin-top: 2px; }}
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
