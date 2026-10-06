"""Shared, palette-adaptive three-dot button backed by editor SVG resources."""
from PySide6.QtCore import QEvent, QSize
from PySide6.QtWidgets import QToolButton, QApplication
from ...resources import ResourceManager

class OptionsButton(QToolButton):
    def __init__(self,parent=None):
        super().__init__(parent)
        self.setProperty("editorOptionsButton",True);self.setAutoRaise(True)
        self._resources=ResourceManager();self.setIconSize(QSize(16,16));self.setFixedSize(24,21)
        self._RefreshIcon()

    def _RefreshIcon(self):
        # Transparent button backgrounds can override the widget's Window role.
        # The application palette remains the authoritative shared theme.
        palette=QApplication.instance().palette()
        theme="light" if palette.window().color().lightness()>128 else "dark"
        self.setIcon(self._resources.Icon(f"icons/{theme}/menu_dots.svg"))

    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type() in (QEvent.Type.PaletteChange,QEvent.Type.StyleChange) and hasattr(self,"_resources"):self._RefreshIcon()
