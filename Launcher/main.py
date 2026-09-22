from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from PySide6.QtCore import QObject, QUrl, Signal, Slot
from PySide6.QtWebChannel import QWebChannel
from PySide6.QtWebEngineCore import QWebEnginePage
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import QApplication, QFileDialog, QMainWindow

from bazzalt.settings import DataPaths
from .catalog import CURRENT_EDITOR_VERSION, HubCatalog

try:
    __compiled__
    IS_COMPILED = True
except NameError:
    IS_COMPILED = False


class HubBridge(QObject):
    StateChanged = Signal()
    OperationFailed = Signal(str)

    def __init__(self, catalog: HubCatalog, parent: QObject | None = None) -> None:
        super().__init__(parent); self.Catalog = catalog

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
        return {"projects": projects, "editors": editors, "projectsRoot": str(DataPaths.Projects()),
                "currentVersion": CURRENT_EDITOR_VERSION}

    @Slot(result=str)
    def GetState(self) -> str: return json.dumps(self._State())

    @Slot(str, result=str)
    def SuggestedProjectPath(self, name: str) -> str:
        safe = "".join(character for character in name if character.isalnum() or character in "_-")
        return str(DataPaths.Projects() / (safe or "NewProject"))

    @Slot(result=str)
    def BrowseProjectLocation(self) -> str:
        DataPaths.Projects().mkdir(parents=True, exist_ok=True)
        return QFileDialog.getExistingDirectory(None, "Project Location", str(DataPaths.Projects()))

    @Slot(result=bool)
    def AddExistingProject(self) -> bool:
        path, _ = QFileDialog.getOpenFileName(None, "Add Project", str(DataPaths.Projects()),
                                              "BAZZALT Projects (*.bproject)")
        if not path: return False
        try: self.Catalog.AddProject(path); self.StateChanged.emit(); return True
        except (OSError, ValueError) as error: self.OperationFailed.emit(str(error)); return False

    @Slot(str, str, str, result=bool)
    def CreateProject(self, name: str, path: str, version: str) -> bool:
        try:
            destination = Path(path).expanduser().resolve()
            if destination.name != name.strip(): raise ValueError("project path must end with the project name")
            self.Catalog.CreateProject(destination.parent, name, version)
            self.StateChanged.emit(); return True
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
        compatible = self.Catalog.CompatibleEditors(project)
        editor = next((item for item in compatible if item["version"] == requested_version), None)
        if editor is None: editor = compatible[0] if compatible else None
        if editor is None: self.OperationFailed.emit("No compatible BAZZALT editor is installed"); return False
        root = Path(editor["root"])
        command = ([editor["command"], "-m", "Editor"] if editor.get("development")
                   else [str(root / editor["command"])])
        command += ["--project", project["path"], "--editor-version", editor["version"]]
        try:
            subprocess.Popen(command, cwd=root, close_fds=True)
            project["last_editor"] = editor["version"]
            project["last_opened"] = datetime.now(timezone.utc).isoformat()
            self.Catalog.Save(); self.StateChanged.emit(); return True
        except OSError as error: self.OperationFailed.emit(str(error)); return False


class LocalPage(QWebEnginePage):
    def acceptNavigationRequest(self, url: QUrl, navigation_type, is_main_frame: bool) -> bool:
        return url.isLocalFile() or url.scheme() in {"qrc", "data", "about"}


class HubWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__(); self.setWindowTitle("BAZZALT Hub"); self.resize(1180, 760)
        self.Catalog = HubCatalog(); self.Catalog.DiscoverEditors()
        if not IS_COMPILED and not getattr(sys, "frozen", False) and os.environ.get("BAZZALT_PRODUCTION") != "1":
            self.Catalog.RegisterDevelopmentEditor(Path(__file__).resolve().parent.parent)
        self.View = QWebEngineView(self); self.View.setPage(LocalPage(self.View))
        self.Channel = QWebChannel(self.View.page()); self.Bridge = HubBridge(self.Catalog, self)
        self.Channel.registerObject("hub", self.Bridge); self.View.page().setWebChannel(self.Channel)
        self.setCentralWidget(self.View)
        self.View.load(QUrl.fromLocalFile(str((Path(__file__).resolve().parent / "index.html").resolve())))


def main() -> int:
    app = QApplication(sys.argv); app.setOrganizationName("BAZZALT"); app.setApplicationName("BAZZALT Hub")
    window = HubWindow(); window.show(); return app.exec()


if __name__ == "__main__": raise SystemExit(main())
