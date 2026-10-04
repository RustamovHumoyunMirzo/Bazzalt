from __future__ import annotations

import sys
import json
import os
import uuid
from pathlib import Path

from bazzalt.settings import DataPaths, ReadProjectMetadata, SettingsStore, Version

HUB_SCHEMA_VERSION = 2
CURRENT_EDITOR_VERSION = "0.5.0"
HUB_VERSION = "1.0.0"
# Existing source-development projects used the unreleased 1.0.0 placeholder.
# Keep that development registration compatible; shipped versions come from editor.json.
DEVELOPMENT_EDITOR_VERSION = "1.0.0"
CURRENT_PROJECT_FORMAT = 1


def _Defaults() -> dict:
    return {"schema_version": HUB_SCHEMA_VERSION, "projects": [], "editors": [], "preferences": {}}


def _MigrateZero(value: dict) -> dict:
    return {"projects": value.get("projects", []), "editors": [], "preferences": {}}


def _MigrateOne(value: dict) -> dict:
    value.setdefault("preferences", {}); return value


class HubCatalog:
    def __init__(self) -> None:
        self.Store = SettingsStore("hub", HUB_SCHEMA_VERSION, _Defaults,
                                   {0: _MigrateZero, 1: _MigrateOne})
        self.Data = self.Store.Load()

    def Save(self) -> None: self.Store.Save(self.Data)

    def RegisterDevelopmentEditor(self, root: Path) -> None:
        if any(item.get("development") for item in self.Data["editors"]): return
        self.Data["editors"].append({"version": DEVELOPMENT_EDITOR_VERSION, "root": str(root.resolve()),
                                      "command": sys.executable, "project_format_max": CURRENT_PROJECT_FORMAT,
                                      "development": True})
        self.Save()

    def DiscoverEditors(self, versions: Path | None = None) -> None:
        versions = versions or DataPaths.Versions()
        discovered: list[dict] = []
        if versions.is_dir():
            for root in versions.glob("bazzalt_*_*_*"):
                try:
                    # Windows PowerShell 5 writes UTF-8 text with a BOM. Accept both
                    # forms so editor discovery also survives manifests from older installers.
                    value = json.loads((root / "editor.json").read_text(encoding="utf-8-sig"))
                    version = str(value["version"]); Version.Parse(version)
                    executable = str(value.get("executable", "Bazzalt.exe" if os.name == "nt" else "Bazzalt"))
                    target = (root / executable).resolve()
                    if not target.is_relative_to(root.resolve()) or not target.is_file(): continue
                    discovered.append({"version": version, "root": str(root.resolve()), "command": executable,
                                       "project_format_max": int(value.get("project_format_max", 1)),
                                       "development": False})
                except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError): continue
        self.Data["editors"] = [item for item in self.Data["editors"] if item.get("development")] + discovered
        self.Save()

    def RemoveProject(self, project_id: str) -> bool:
        old = len(self.Data["projects"])
        self.Data["projects"] = [item for item in self.Data["projects"] if item.get("id") != project_id]
        changed = len(self.Data["projects"]) != old
        if changed: self.Save()
        return changed

    def AddProject(self, path: str | Path) -> dict:
        metadata = ReadProjectMetadata(path)
        normalized = metadata["path"]
        existing = next((item for item in self.Data["projects"] if item["path"] == normalized), None)
        record = {"id": existing["id"] if existing else str(uuid.uuid4()),
                  "name": metadata["name"], "path": normalized,
                  "format_version": metadata["format_version"],
                  "last_editor": existing.get("last_editor", "") if existing else ""}
        if existing: existing.update(record)
        else: self.Data["projects"].append(record)
        self.Save(); return record

    def CreateProject(self, directory: str | Path, name: str,
                      editor_version: str = CURRENT_EDITOR_VERSION) -> dict:
        if (not name.strip() or name.strip() in {".", ".."} or
                any(character in name for character in ("/", "\\", ":", "\0"))):
            raise ValueError("project name contains invalid path characters")
        root = Path(directory).resolve() / name.strip()
        root.mkdir(parents=True, exist_ok=False)
        (root / "Assets").mkdir()
        project = root / f"{name.strip()}.bproject"
        project.write_text(
            f'FormatVersion: {CURRENT_PROJECT_FORMAT}\nProjectUUID: "{uuid.uuid4()}"\n'
            f'Name: {json.dumps(name.strip(), ensure_ascii=False)}\nAssetDirectory: "Assets"\nStartupScene: ""\n'
            f'Properties:\n  "engine.version": "{editor_version}"\n', encoding="utf-8")
        return self.AddProject(project)

    def CompatibleEditors(self, project: dict) -> list[dict]:
        required = int(project.get("format_version", 0))
        values = [item for item in self.Data["editors"]
                  if required <= int(item.get("project_format_max", 0))]
        return sorted(values, key=lambda item: Version.Parse(item["version"]), reverse=True)


__all__ = ["CURRENT_EDITOR_VERSION", "HubCatalog"]
