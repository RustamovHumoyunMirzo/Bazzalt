from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication, QComboBox, QFileDialog, QHBoxLayout, QInputDialog, QLabel, QListWidget,
    QMessageBox, QPushButton, QVBoxLayout, QWidget,
)

from .catalog import HubCatalog


class HubWindow(QWidget):
    def __init__(self) -> None:
        super().__init__(); self.setWindowTitle("BAZZALT Hub"); self.resize(760, 480)
        self.Catalog = HubCatalog()
        self.Catalog.RegisterDevelopmentEditor(Path(__file__).resolve().parent.parent)
        layout = QVBoxLayout(self)
        title = QLabel("Projects"); title.setStyleSheet("font-size: 22px; font-weight: 600")
        layout.addWidget(title)
        self.Projects = QListWidget(); layout.addWidget(self.Projects, 1)
        row = QHBoxLayout(); self.Version = QComboBox(); row.addWidget(self.Version, 1)
        self.AddButton = QPushButton("Add Existing"); self.NewButton = QPushButton("New Project")
        self.OpenButton = QPushButton("Open")
        row.addWidget(self.AddButton); row.addWidget(self.NewButton); row.addWidget(self.OpenButton)
        layout.addLayout(row)
        self.Projects.currentRowChanged.connect(self._SelectionChanged)
        self.Projects.itemDoubleClicked.connect(lambda _item: self.Launch())
        self.AddButton.clicked.connect(self.AddExisting)
        self.NewButton.clicked.connect(self.CreateProject)
        self.OpenButton.clicked.connect(self.Launch)
        self.Refresh()

    def Refresh(self) -> None:
        self.Projects.clear()
        for project in self.Catalog.Data["projects"]:
            self.Projects.addItem(f"{project['name']}\n{project['path']}")
        if self.Projects.count(): self.Projects.setCurrentRow(0)
        else: self._SelectionChanged(-1)

    def _SelectionChanged(self, row: int) -> None:
        self.Version.clear(); enabled = 0 <= row < len(self.Catalog.Data["projects"])
        if enabled:
            for editor in self.Catalog.CompatibleEditors(self.Catalog.Data["projects"][row]):
                self.Version.addItem(editor["version"], editor)
        self.OpenButton.setEnabled(enabled and self.Version.count() > 0)

    def AddExisting(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Add Project", "", "BAZZALT Projects (*.bproject)")
        if not path: return
        try: self.Catalog.AddProject(path); self.Refresh()
        except (OSError, ValueError) as error: QMessageBox.critical(self, "Invalid Project", str(error))

    def CreateProject(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, "Project Location")
        if not directory: return
        name, accepted = QInputDialog.getText(self, "New Project", "Project name:")
        if not accepted or not name.strip(): return
        try: self.Catalog.CreateProject(directory, name); self.Refresh()
        except (OSError, ValueError) as error: QMessageBox.critical(self, "Could Not Create Project", str(error))

    def Launch(self) -> None:
        row = self.Projects.currentRow()
        if row < 0 or self.Version.currentIndex() < 0: return
        project = self.Catalog.Data["projects"][row]
        editor = self.Version.currentData()
        root = Path(editor["root"])
        if editor.get("development"):
            command = [editor["command"], "-m", "Editor", "--project", project["path"],
                       "--editor-version", editor["version"]]
        else:
            command = [str(root / editor["command"]), "--project", project["path"],
                       "--editor-version", editor["version"]]
        try:
            subprocess.Popen(command, cwd=root, close_fds=True)
            project["last_editor"] = editor["version"]; self.Catalog.Save()
        except OSError as error: QMessageBox.critical(self, "Launch Failed", str(error))


def main() -> int:
    app = QApplication(sys.argv); app.setOrganizationName("BAZZALT"); app.setApplicationName("BAZZALT Hub")
    window = HubWindow(); window.show(); return app.exec()


if __name__ == "__main__": raise SystemExit(main())
