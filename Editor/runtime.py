"""Private Python facade for the optional pybind11 editor runtime module."""

from __future__ import annotations

import importlib
import sys
from pathlib import Path

from PySide6.QtCore import QObject, Signal


def _LoadNativeModule():
    try:
        return importlib.import_module("Editor._bazzalt_runtime")
    except ImportError:
        root = Path(__file__).resolve().parent.parent
        candidates = (root / "build" / "Editor", root / "build" / "Editor" / "Debug",
                      root / "build" / "Editor" / "Release")
        for directory in candidates:
            if directory.is_dir() and str(directory) not in sys.path:
                sys.path.insert(0, str(directory))
        try:
            return importlib.import_module("_bazzalt_runtime")
        except ImportError:
            return None


class RuntimeService(QObject):
    SceneChanged = Signal()
    ProjectChanged = Signal(str)
    ErrorOccurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        module = _LoadNativeModule()
        self._host = module.EditorHost() if module is not None else None

    def IsAvailable(self) -> bool:
        return self._host is not None

    def LastError(self) -> str:
        return self._host.last_error() if self._host is not None else "Native editor runtime is not built"

    def LoadProject(self, path: str | Path) -> bool:
        if self._host is None or not self._host.load_project(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.ProjectChanged.emit(str(path)); self.SceneChanged.emit(); return True

    def CreateProject(self, path: str | Path) -> bool:
        if self._host is None or not self._host.create_project(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.ProjectChanged.emit(str(path)); self.SceneChanged.emit(); return True

    def SaveProject(self, path: str | Path | None = None) -> bool:
        if self._host is None or not self._host.save_project(str(path or "")):
            self.ErrorOccurred.emit(self.LastError()); return False
        return True

    def LoadScene(self, path: str | Path) -> bool:
        if self._host is None or not self._host.load_scene(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.SceneChanged.emit(); return True

    def SaveScene(self, path: str | Path) -> bool:
        if self._host is None or not self._host.save_scene(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        return True

    def NewScene(self) -> None:
        if self._host is not None: self._host.new_scene(); self.SceneChanged.emit()

    def Entities(self) -> list[dict]:
        return list(self._host.entities()) if self._host is not None else []

    def EntityDetails(self, entity_id: str) -> dict:
        return dict(self._host.entity_details(entity_id)) if self._host is not None else {}

    def CreateEntity(self, name: str, parent: str = "") -> str:
        value = self._host.create_entity(name, parent) if self._host is not None else ""
        self.SceneChanged.emit(); return value

    def DestroyEntity(self, entity_id: str) -> bool:
        result = bool(self._host and self._host.destroy_entity(entity_id))
        if result: self.SceneChanged.emit()
        return result

    def SetTransform(self, entity_id: str, position, rotation, scale) -> bool:
        return bool(self._host and self._host.set_transform(entity_id, position, rotation, scale))

    def Translate(self, entity_id: str, delta) -> bool:
        return bool(self._host and self._host.translate(entity_id, delta))

    def AssetDirectory(self) -> str:
        return self._host.asset_directory() if self._host is not None else ""

    def Play(self) -> bool:
        return bool(self._host and self._host.play())

    def Pause(self, paused: bool) -> None:
        if self._host is not None: self._host.pause(paused)

    def Step(self) -> None:
        if self._host is not None: self._host.step()

    def Tick(self) -> None:
        if self._host is not None: self._host.tick()

    def Stop(self) -> None:
        if self._host is not None: self._host.stop(); self.SceneChanged.emit()


__all__ = ["RuntimeService"]
