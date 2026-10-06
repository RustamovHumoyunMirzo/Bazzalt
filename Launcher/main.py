from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from PySide6.QtCore import QObject, Signal, Slot
from PySide6.QtWidgets import QApplication, QFileDialog

from bazzalt.settings import DataPaths
from bazzalt.branding import LogoIcon
from .catalog import CURRENT_EDITOR_VERSION, HubCatalog
from .ui import NativeHubWindow

try:
    __compiled__
    IS_COMPILED = True
except NameError:
    IS_COMPILED = False


class HubBridge(QObject):
    StateChanged = Signal()
    OperationFailed = Signal(str)
    ProjectAdded = Signal(str)

    def __init__(self, catalog: HubCatalog, parent: QObject | None = None) -> None:
        super().__init__(parent); self.Catalog = catalog

    def ProjectsRoot(self) -> Path:
        value=self.Catalog.Data.get("preferences",{}).get("projects_root","")
        return Path(value).expanduser().resolve() if value else DataPaths.Projects()

    def _State(self) -> dict:
        projects = []
        for value in self.Catalog.Data["projects"]:
            project = dict(value)
            project["version"] = project.get("last_editor") or CURRENT_EDITOR_VERSION
            project["lastOpened"] = project.get("last_opened", "")
            project["compatibleEditors"] = [item["version"] for item in self.Catalog.CompatibleEditors(project)]
            projects.append(project)
        editors = [{key: item.get(key) for key in ("version", "project_format_max")}
                   for item in self.Catalog.Data["editors"]]
        return {"projects": projects, "editors": editors, "projectsRoot": str(self.ProjectsRoot()),
                "currentVersion": CURRENT_EDITOR_VERSION}

    @Slot(result=str)
    def GetState(self) -> str: return json.dumps(self._State())

    @Slot(str, result=str)
    def SuggestedProjectPath(self, name: str) -> str:
        safe = "".join(character for character in name if character.isalnum() or character in "_-")
        return str(self.ProjectsRoot() / (safe or "NewProject"))

    @Slot(result=str)
    def BrowseProjectLocation(self) -> str:
        return QFileDialog.getExistingDirectory(self.parent(), "Project Location", str(self.ProjectsRoot()))

    @Slot(result=bool)
    def AddExistingProject(self) -> bool:
        path, _ = QFileDialog.getOpenFileName(self.parent(), "Add Project", str(self.ProjectsRoot()),
                                              "BAZZALT Projects (*.bproject)")
        if not path: return False
        try:
            record=self.Catalog.AddProject(path); self.StateChanged.emit(); self.ProjectAdded.emit(record["id"]); return True
        except (OSError, ValueError) as error: self.OperationFailed.emit(str(error)); return False

    @Slot(str, str, str, result=bool)
    def CreateProject(self, name: str, path: str, version: str) -> bool:
        try:
            destination = Path(path).expanduser().resolve()
            if destination.name != name.strip(): raise ValueError("project path must end with the project name")
            if not any(editor["version"]==version for editor in self.Catalog.Data["editors"]):
                raise ValueError("Select an installed editor version before creating a project")
            record=self.Catalog.CreateProject(destination.parent, name, version)
            self.StateChanged.emit(); self.ProjectAdded.emit(record["id"]); return True
        except (OSError, ValueError) as error: self.OperationFailed.emit(str(error)); return False

    @Slot(str, result=bool)
    def RemoveProject(self, project_id: str) -> bool:
        changed = self.Catalog.RemoveProject(project_id)
        if changed: self.StateChanged.emit()
        return changed

    @Slot(str, str, result=bool)
    def LaunchProject(self, project_id: str, requested_version: str) -> bool:
        project = next((item for item in self.Catalog.Data["projects"] if item.get("id") == project_id), None)
        if project is None: self.OperationFailed.emit("Project is no longer registered"); return False
        if not Path(project["path"]).is_file(): self.OperationFailed.emit("The project file no longer exists. Locate it again using Add Project."); return False
        compatible = self.Catalog.CompatibleEditors(project)
        editor = next((item for item in compatible if item["version"] == requested_version), None)
        if editor is None and not requested_version: editor = compatible[0] if compatible else None
        if editor is None: self.OperationFailed.emit("No compatible BAZZALT editor is installed"); return False
        root = Path(editor["root"])
        command = ([editor["command"], "-m", "Editor"] if editor.get("development")
                   else [str(root / editor["command"])])
        command += ["--project", project["path"], "--editor-version", editor["version"]]
        try:
            subprocess.Popen(command, cwd=root, close_fds=True,
                             creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
            project["last_editor"] = editor["version"]
            project["last_opened"] = datetime.now(timezone.utc).isoformat()
            self.Catalog.Save(); self.StateChanged.emit(); return True
        except OSError as error: self.OperationFailed.emit(str(error)); return False


class HubWindow(NativeHubWindow):
    def __init__(self, catalog: HubCatalog | None = None) -> None:
        supplied=catalog is not None
        catalog=catalog or HubCatalog()
        versions=catalog.Data.get("preferences",{}).get("versions_root","")
        catalog.DiscoverEditors(Path(versions) if versions else None)
        if not supplied and not IS_COMPILED and not getattr(sys,"frozen",False) and os.environ.get("BAZZALT_PRODUCTION")!="1":
            catalog.RegisterDevelopmentEditor(Path(__file__).resolve().parent.parent)
        super().__init__(catalog, HubBridge(catalog))


def main() -> int:
    import struct
    if struct.calcsize("P") != 8:
        raise RuntimeError("BAZZALT Hub requires a 64-bit machine and Python runtime.")
    app = QApplication(sys.argv); app.setOrganizationName("BAZZALT"); app.setApplicationName("BAZZALT Hub")
    app.setWindowIcon(LogoIcon("#707070"))
    window = HubWindow(); window.show(); return app.exec()


if __name__ == "__main__": raise SystemExit(main())
