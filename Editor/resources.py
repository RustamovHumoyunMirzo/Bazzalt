"""Safe access to editor-owned files such as icons, SVGs, and locale catalogs."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QIcon, QPixmap
from bazzalt.resources import Package, ResourcePackage


class ResourceManager:
    """Source files during development; embedded Qt resources in production."""

    def __init__(self, root: Path | None = None) -> None:
        self._root = (root or Path(__file__).resolve().parent / "assets").resolve()
        self._package = ResourcePackage("editor", self._root, compiled=False) if root is not None else Package("editor")
        self._icons = {}

    @property
    def Root(self) -> Path:
        return self._root

    def Resolve(self, relative_path: str | Path, *, required: bool = True) -> Path | str:
        path = self._package.Path(relative_path, required=required)
        return path if self._package.Compiled else Path(path)

    def Path(self, relative_path: str | Path, *, required: bool = True) -> str:
        """Qt-compatible filename: source file in development, qrc path in production."""
        return self._package.Path(relative_path, required=required)

    def ReadBytes(self, relative_path: str | Path) -> bytes:
        return self._package.ReadBytes(relative_path)

    def ReadText(self, relative_path: str | Path, encoding: str = "utf-8") -> str:
        return self._package.ReadText(relative_path, encoding)

    def Icon(self, relative_path: str | Path) -> QIcon:
        key=str(relative_path)
        if key not in self._icons:
            if len(self._icons)>=256:self._icons.clear()
            self._icons[key]=QIcon(self.Path(relative_path))
        return QIcon(self._icons[key])

    def Pixmap(self, relative_path: str | Path) -> QPixmap:
        return QPixmap(self.Path(relative_path))


__all__ = ["ResourceManager"]
