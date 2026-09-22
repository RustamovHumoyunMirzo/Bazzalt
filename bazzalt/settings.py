from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from PySide6.QtCore import QCoreApplication, QIODevice, QSaveFile, QStandardPaths


class DataPaths:
    @staticmethod
    def Root() -> Path:
        base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericDataLocation)
        return Path(base) / "BAZZALT" / "data"

    @staticmethod
    def LegacyRoot() -> Path:
        base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericDataLocation)
        return Path(base) / "BAZZALT"

    @classmethod
    def Config(cls) -> Path: return cls.Root() / "Config"
    @classmethod
    def Editors(cls) -> Path: return cls.Root() / "Editors"
    @classmethod
    def Logs(cls) -> Path: return cls.Root() / "Logs"
    @staticmethod
    def Projects() -> Path:
        base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DocumentsLocation)
        return Path(base) / "BazzaltProjects"
    @staticmethod
    def Program() -> Path:
        instance = QCoreApplication.instance()
        return Path(instance.applicationDirPath()) if instance is not None else Path.cwd()
    @classmethod
    def Versions(cls) -> Path: return cls.Program() / "versions"


class SettingsStore:
    """Versioned JSON settings with bounded reads, migrations, and atomic writes."""

    MaxBytes = 4 * 1024 * 1024

    def __init__(self, name: str, version: int,
                 defaults: Callable[[], dict], migrations: dict[int, Callable[[dict], dict]] | None = None) -> None:
        if not name or any(part in name for part in ("/", "\\", "..")):
            raise ValueError("invalid settings name")
        self.Path = DataPaths.Config() / f"{name}.json"
        self.Version = version
        self._defaults = defaults
        self._migrations = migrations or {}

    def Load(self) -> dict:
        source = self.Path
        if not self.Path.exists():
            legacy = DataPaths.LegacyRoot() / "Config" / self.Path.name
            if legacy != self.Path and legacy.is_file(): source = legacy
            else: return self._defaults()
        try:
            if source.stat().st_size > self.MaxBytes: raise ValueError("settings file is too large")
            value = json.loads(source.read_text(encoding="utf-8"))
            if not isinstance(value, dict): raise ValueError("settings root must be an object")
            version = int(value.get("schema_version", 0))
            if version > self.Version: raise ValueError("settings were written by a newer application")
            while version < self.Version:
                migration = self._migrations.get(version)
                if migration is None: raise ValueError(f"missing settings migration {version}")
                value = migration(value); version += 1; value["schema_version"] = version
            if source != self.Path: self.Save(value)
            return value
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            return self._defaults()

    def Save(self, value: dict) -> None:
        payload = dict(value); payload["schema_version"] = self.Version
        encoded = json.dumps(payload, indent=2, ensure_ascii=False).encode("utf-8")
        if len(encoded) > self.MaxBytes: raise ValueError("settings payload is too large")
        self.Path.parent.mkdir(parents=True, exist_ok=True)
        target = QSaveFile(str(self.Path))
        if not target.open(QIODevice.OpenModeFlag.WriteOnly): raise OSError(target.errorString())
        if target.write(encoded) != len(encoded) or not target.commit():
            raise OSError(target.errorString())


@dataclass(frozen=True, order=True)
class Version:
    Major: int
    Minor: int
    Patch: int

    @classmethod
    def Parse(cls, value: str) -> "Version":
        pieces = value.split(".")
        if len(pieces) != 3 or not all(piece.isdigit() for piece in pieces):
            raise ValueError("version must use major.minor.patch")
        return cls(*(int(piece) for piece in pieces))

    def __str__(self) -> str: return f"{self.Major}.{self.Minor}.{self.Patch}"


def ReadProjectMetadata(path: str | Path) -> dict:
    project = Path(path).resolve()
    if project.suffix.lower() != ".bproject" or not project.is_file():
        raise ValueError("project must be an existing .bproject file")
    if project.stat().st_size > 4 * 1024 * 1024: raise ValueError("project metadata is too large")
    result: dict[str, object] = {"path": str(project), "properties": {}}
    in_properties = False
    for raw in project.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"): continue
        if raw == "Properties:": in_properties = True; continue
        if ":" not in raw: continue
        key, value = raw.strip().split(":", 1)
        value = value.strip().strip('"').replace('\\"', '"').replace('\\\\', '\\')
        if in_properties and raw.startswith("  "):
            result["properties"][key.strip('"')] = value  # type: ignore[index]
        else:
            in_properties = False; result[key] = value
    result["format_version"] = int(result.get("FormatVersion", 0))
    result["name"] = str(result.get("Name", project.stem))
    asset = Path(str(result.get("AssetDirectory", "Assets")))
    result["asset_directory"] = str((project.parent / asset).resolve())
    return result


__all__ = ["DataPaths", "ReadProjectMetadata", "SettingsStore", "Version"]
