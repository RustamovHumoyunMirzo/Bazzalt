"""Shared, palette-adaptive three-dot button backed by editor SVG resources."""
from PySide6.QtCore import QEvent, QSize
from PySide6.QtWidgets import QToolButton
from ...resources import ResourceManager

class OptionsButton(QToolButton):
    def __init__(self,parent=None):
        super().__init__(parent)
        self._resources=ResourceManager();self.setIconSize(QSize(16,16));self.setFixedSize(24,24)
        self._RefreshIcon()

    def _RefreshIcon(self):
        theme="light" if self.palette().window().color().lightness()>128 else "dark"
        self.setIcon(self._resources.Icon(f"icons/{theme}/menu_dots.svg"))

    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type()==QEvent.Type.PaletteChange and hasattr(self,"_resources"):self._RefreshIcon()
