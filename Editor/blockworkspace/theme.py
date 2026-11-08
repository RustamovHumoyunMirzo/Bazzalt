"""Theme configuration (colors, fonts, category palette)."""
from __future__ import annotations
from dataclasses import dataclass, field, replace
from PySide6.QtGui import QColor

SCRATCH_CATEGORY_COLORS = {
    "Events": "#FFBF00", "Control": "#FFAB19", "Motion": "#4C97FF", "Movement": "#4C97FF",
    "Looks": "#9966FF", "Sound": "#CF63CF", "Sensing": "#5CB1D6", "Logic": "#5CB1D6",
    "Math": "#59C059", "Operators": "#59C059", "Text": "#CF63CF", "Variables": "#FF8C1A",
    "Functions": "#FF6680", "Combat": "#FF6680", "Debug": "#8A8A8A",
}

_cache: dict[str, QColor] = {}


def Qc(value) -> QColor:
    """Cached QColor from hex string (or pass-through for QColor)."""
    if isinstance(value, QColor):
        return QColor(value)
    c = _cache.get(value)
    if c is None:
        c = QColor(value)
        if not c.isValid():
            c = QColor("#8A8A8A")
        _cache[value] = c
    return QColor(c)


@dataclass
class BlockTheme:
    Name: str = "Scratch"
    Background: str = "#F4F5F7"
    GridMinor: str = "#E9EBEE"
    GridMajor: str = "#DADDE2"
    TextColor: str = "#FFFFFF"
    FieldFill: str = "#FFFFFF"
    FieldText: str = "#575E75"
    FieldPlaceholder: str = "#A0A6BA"
    SelectionColor: str = "#FFD84D"
    PreviewColor: str = "#FFFFFF"
    InsertionHighlight: str = "#FFE45C"
    RubberBandColor: str = "#4C97FF"
    DisabledColor: str = "#B4B8C0"
    DisabledText: str = "#F1F2F4"
    WarningColor: str = "#FF9800"
    ErrorColor: str = "#E53935"
    CommentFill: str = "#FFF3A3"
    CommentText: str = "#5D4B00"
    ToolboxBackground: str = "#FFFFFF"
    ToolboxText: str = "#575E75"
    ToolboxSelected: str = "#E9EEF2"
    ToolboxBorder: str = "#DDE1E6"
    FlyoutBackground: str = "#F9FAFB"
    SearchBackground: str = "#F0F2F5"
    FontFamily: str = "Helvetica Neue"
    FontPixelSize: int = 13
    OutlineDarken: int = 118
    CornerRadius: float = 5.0
    DefaultBlockColor: str = "#8A8A8A"
    CategoryColors: dict = field(default_factory=lambda: dict(SCRATCH_CATEGORY_COLORS))

    def CategoryColor(self, name: str) -> str:
        return self.CategoryColors.get(name, self.DefaultBlockColor)

    def SetCategoryColor(self, name: str, color: str):
        self.CategoryColors[name] = color

    def Copy(self) -> "BlockTheme":
        return replace(self, CategoryColors=dict(self.CategoryColors))

    @staticmethod
    def Light() -> "BlockTheme":
        return BlockTheme()

    @staticmethod
    def Dark() -> "BlockTheme":
        return BlockTheme(
            Name="Dark", Background="#1E2024", GridMinor="#25282D", GridMajor="#2E3238",
            FieldFill="#2B2E35", FieldText="#E6E8EE", FieldPlaceholder="#7C8294",
            DisabledColor="#4A4E57", DisabledText="#9096A3", ToolboxBackground="#25282D",
            ToolboxText="#D6D9E0", ToolboxSelected="#33373F", ToolboxBorder="#3A3E46",
            FlyoutBackground="#1E2024", SearchBackground="#33373F", CommentFill="#5B5320",
            CommentText="#FFF3A3", OutlineDarken=135,
        )

    @staticmethod
    def HighContrast() -> "BlockTheme":
        t = BlockTheme.Dark()
        t.Name = "HighContrast"
        t.Background = "#000000"
        t.GridMinor = "#111111"
        t.GridMajor = "#222222"
        t.SelectionColor = "#FFFFFF"
        t.OutlineDarken = 200
        return t
