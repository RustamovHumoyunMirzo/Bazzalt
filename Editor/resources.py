"""Safe access to editor-owned files such as icons, SVGs, and locale catalogs."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QIcon, QPixmap


class ResourceManager:
    """Resolves files below ``Editor/assets`` without exposing arbitrary paths."""

    def __init__(self, root: Path | None = None) -> None:
        self._root = (root or Path(__file__).resolve().parent / "assets").resolve()

    @property
    def Root(self) -> Path:
        return self._root

    def Resolve(self, relative_path: str | Path, *, required: bool = True) -> Path:
        path = (self._root / relative_path).resolve()
        try:
            path.relative_to(self._root)
        except ValueError as error:
            raise ValueError("Resource path must stay inside Editor/assets") from error
        if required and not path.is_file():
            raise FileNotFoundError(f"Editor resource does not exist: {relative_path}")
        return path

    def ReadText(self, relative_path: str | Path, encoding: str = "utf-8") -> str:
        return self.Resolve(relative_path).read_text(encoding=encoding)

    def Icon(self, relative_path: str | Path) -> QIcon:
        return QIcon(str(self.Resolve(relative_path)))

    def Pixmap(self, relative_path: str | Path) -> QPixmap:
        return QPixmap(str(self.Resolve(relative_path)))


__all__ = ["ResourceManager"]
