"""Private Python facade for the optional pybind11 editor runtime module."""

from __future__ import annotations

import importlib
import importlib.util
import os
import sys
from pathlib import Path

from PySide6.QtCore import QObject, Signal


_NATIVE_LOAD_ERROR = ""
_DLL_DIRECTORIES: list[object] = []


def _LoadExtension(path: Path):
    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        for directory in (Path(sys.executable).resolve().parent, path.parent):
            if directory.is_dir(): _DLL_DIRECTORIES.append(os.add_dll_directory(str(directory)))
    spec = importlib.util.spec_from_file_location("_bazzalt_runtime", path)
    if spec is None or spec.loader is None: raise ImportError(f"cannot create loader for {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _LoadNativeModule():
    global _NATIVE_LOAD_ERROR
    errors: list[str] = []
    try:
        return importlib.import_module("Editor._bazzalt_runtime")
    except ImportError as error:
        errors.append(str(error))
        source_root = Path(__file__).resolve().parent.parent
        program_root = Path(sys.executable).resolve().parent
        candidates = (program_root / "Editor", program_root, source_root / "Editor",
                      source_root / "build" / "Editor", source_root / "build" / "Editor" / "Debug",
                      source_root / "build" / "Editor" / "Release")
        for directory in candidates:
            for path in directory.glob("_bazzalt_runtime*.pyd" if os.name == "nt" else "_bazzalt_runtime*.so"):
                try: return _LoadExtension(path)
                except (ImportError, OSError) as native_error: errors.append(f"{path}: {native_error}")
        try:
            return importlib.import_module("_bazzalt_runtime")
        except ImportError as fallback_error:
            errors.append(str(fallback_error))
            _NATIVE_LOAD_ERROR = "; ".join(errors)
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
        if self._host is not None: return self._host.last_error()
        return f"Native editor runtime could not be loaded: {_NATIVE_LOAD_ERROR or 'module not found'}"

    def LoadProject(self, path: str | Path) -> bool:
        if self._host is None or not self._host.load_project(str(path)):
            self.ErrorOccurred.emit(self.LastError()); return False
        self.ProjectChanged.emit(str(path)); self.SceneChanged.emit(); return True

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

    def SceneInfo(self) -> dict:
        if self._host is not None and hasattr(self._host, "scene_info"):
            return dict(self._host.scene_info())
        return {"uuid": "0", "name": "Untitled"}

    def ComponentTypes(self) -> list[str]:
        return list(self._host.component_types()) if self._host is not None and hasattr(self._host, "component_types") else []

    def AddComponent(self, entity_id: str, component_type: str) -> bool:
        result = bool(self._host and hasattr(self._host, "add_component") and self._host.add_component(entity_id, component_type))
        if result: self.SceneChanged.emit()
        return result

    def SetParent(self, entity_id: str, parent_id: str = "") -> bool:
        result = bool(self._host and self._host.set_parent(entity_id, parent_id))
        if result: self.SceneChanged.emit()
        return result

    def CreateViewport(self, viewport_id: int, handle: int, scene: bool,
                       width: int, height: int) -> bool:
        return bool(self._host and hasattr(self._host, "create_viewport") and self._host.create_viewport(
            viewport_id, handle, scene, width, height))

    def ResizeViewport(self, viewport_id: int, width: int, height: int) -> None:
        if self._host is not None and hasattr(self._host, "resize_viewport"): self._host.resize_viewport(viewport_id, width, height)

    def DestroyViewport(self, viewport_id: int) -> None:
        if self._host is not None and hasattr(self._host, "destroy_viewport"): self._host.destroy_viewport(viewport_id)

    def SetSceneCamera(self, viewport_id: int, eye, target) -> None:
        if self._host is not None and hasattr(self._host, "set_scene_camera"): self._host.set_scene_camera(viewport_id, eye, target)

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
