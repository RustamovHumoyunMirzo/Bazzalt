"""Icon loading: SVG/PNG files, inline SVG strings, built-in icons, QIcon/QPixmap, callables."""
from __future__ import annotations
import os
from PySide6.QtCore import Qt, QByteArray, QRectF
from PySide6.QtGui import QPixmap, QPainter, QIcon, QColor
from PySide6.QtSvg import QSvgRenderer

BUILTIN_ICONS = {
    "flag": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M5 2h2v20H5zM8 3h12l-3 5 3 5H8z"/></svg>',
    "gear": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M12 8a4 4 0 100 8 4 4 0 000-8zm9 5v-2l-2.2-.5a7 7 0 00-.8-1.9l1.2-1.9-1.4-1.4-1.9 1.2a7 7 0 00-1.9-.8L13 3h-2l-.5 2.2a7 7 0 00-1.9.8L6.7 4.8 5.3 6.2l1.2 1.9a7 7 0 00-.8 1.9L3.5 11v2l2.2.5c.2.7.5 1.3.8 1.9l-1.2 1.9 1.4 1.4 1.9-1.2c.6.3 1.2.6 1.9.8L11 21h2l.5-2.2c.7-.2 1.3-.5 1.9-.8l1.9 1.2 1.4-1.4-1.2-1.9c.3-.6.6-1.2.8-1.9z"/></svg>',
    "arrow": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M4 11h11.2l-4.6-4.6L12 5l7 7-7 7-1.4-1.4 4.6-4.6H4z"/></svg>',
    "eye": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M12 5C6 5 2 12 2 12s4 7 10 7 10-7 10-7-4-7-10-7zm0 11a4 4 0 110-8 4 4 0 010 8zm0-2a2 2 0 100-4 2 2 0 000 4z"/></svg>',
    "sword": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M20 2l2 2-9.5 9.5-2-2zM8 12l4 4-2 2 1.5 1.5-2 2L8 19l-3 3-2-2 3-3-2.5-2.5 2-2L7 14z"/></svg>',
    "repeat": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M7 7h10v3l4-4-4-4v3H5v6h2zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2z"/></svg>',
    "clock": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2a10 10 0 100 20 10 10 0 000-20zm1 5v5.4l4 2.4-1 1.7-5-3V7z"/></svg>',
    "footsteps": '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M8 3c2 0 3 2 3 4s-1 4-3 4-3-2-3-4 1-4 3-4zm-3 10h6v3a3 3 0 01-6 0zM17 8c2 0 3 2 3 4s-1 4-3 4-3-2-3-4 1-4 3-4zm-3 10h6v1a3 3 0 01-6 0z"/></svg>',
}


class IconProvider:
    def __init__(self):
        self._cache: dict = {}

    def Pixmap(self, icon, size: int = 16, color: str = "#FFFFFF", dpr: float = 2.0):
        if icon is None:
            return None
        key = (icon if isinstance(icon, (str, int)) else id(icon), size, color)
        if key in self._cache:
            return self._cache[key]
        pm = self._Load(icon, size, color, dpr)
        self._cache[key] = pm
        return pm

    def _Load(self, icon, size, color, dpr):
        px = int(size * dpr)
        if callable(icon):
            r = icon(size)
            return r if isinstance(r, QPixmap) else None
        if isinstance(icon, QPixmap):
            return icon.scaled(px, px, Qt.KeepAspectRatio, Qt.SmoothTransformation)
        if isinstance(icon, QIcon):
            return icon.pixmap(px, px)
        if isinstance(icon, str):
            svg = None
            if icon.lstrip().startswith("<svg"):
                svg = icon
            elif icon.startswith("builtin:"):
                svg = BUILTIN_ICONS.get(icon[8:])
            elif icon in BUILTIN_ICONS:
                svg = BUILTIN_ICONS[icon]
            elif os.path.isfile(icon):
                if icon.lower().endswith(".svg"):
                    with open(icon, "r", encoding="utf-8") as f:
                        svg = f.read()
                else:
                    pm = QPixmap(icon)
                    return pm.scaled(px, px, Qt.KeepAspectRatio, Qt.SmoothTransformation) if not pm.isNull() else None
            if svg:
                svg = svg.replace("currentColor", color)
                r = QSvgRenderer(QByteArray(svg.encode("utf-8")))
                if not r.isValid():
                    return None
                pm = QPixmap(px, px)
                pm.fill(Qt.transparent)
                p = QPainter(pm)
                p.setRenderHint(QPainter.Antialiasing)
                r.render(p, QRectF(0, 0, px, px))
                p.end()
                pm.setDevicePixelRatio(dpr)
                return pm
        return None
