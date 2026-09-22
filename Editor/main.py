from __future__ import annotations
import argparse, os, sys
from pathlib import Path
from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import QApplication, QLabel, QMessageBox, QProgressBar, QVBoxLayout, QWidget

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from Editor.gui.application import Editor
    from Editor.localization import LocalizationManager
    from Editor.resources import ResourceManager
    from Editor.runtime import RuntimeService
    from Editor.theme import Theme, ThemeManager
else:
    from .gui.application import Editor
    from .localization import LocalizationManager
    from .resources import ResourceManager
    from .runtime import RuntimeService
    from .theme import Theme, ThemeManager
from bazzalt.settings import ReadProjectMetadata, SettingsStore, Version

EDITOR_PROJECT_FORMAT_MAX = 1

class ProjectScanner(QThread):
    Progress = Signal(int, str); Completed = Signal(object); Failed = Signal(str)
    def __init__(self, project: Path, version: Version) -> None:
        super().__init__(); self.Project=project; self.Version=version
    def run(self) -> None:
        try:
            metadata=ReadProjectMetadata(self.Project)
            if not 1 <= int(metadata["format_version"]) <= EDITOR_PROJECT_FORMAT_MAX:
                raise ValueError("project format is not supported by this editor version")
            required=metadata["properties"].get("engine.version", "")
            if required and Version.Parse(str(required)).Major > self.Version.Major:
                raise ValueError(f"project requires editor {required} or a compatible newer major version")
            assets=Path(str(metadata["asset_directory"])); files=[]
            if assets.is_dir():
                for root, directories, names in os.walk(assets, followlinks=False):
                    directories[:]=[name for name in directories if not (Path(root)/name).is_symlink()]
                    files.extend(Path(root)/name for name in names)
                    if len(files)>1_000_000: raise ValueError("project contains too many files")
            if not files:self.Progress.emit(85,"Scanning empty asset directory")
            for index,path in enumerate(files,1):self.Progress.emit(min(85,index*85//len(files)),path.name)
            self.Completed.emit(metadata)
        except (OSError,ValueError) as error:self.Failed.emit(str(error))

class LoadingWindow(QWidget):
    Loaded=Signal(object)
    def __init__(self,project:Path,version:Version,runtime:RuntimeService)->None:
        super().__init__();self.Project=project;self.Runtime=runtime;self.setWindowTitle("Loading BAZZALT Project");self.setFixedSize(480,130)
        layout=QVBoxLayout(self);self.Title=QLabel(project.stem);self.Status=QLabel("Validating project…");self.Progress=QProgressBar();self.Progress.setRange(0,100)
        layout.addWidget(self.Title);layout.addWidget(self.Status);layout.addWidget(self.Progress)
        self.Scanner=ProjectScanner(project,version);self.Scanner.Progress.connect(self._Progress);self.Scanner.Completed.connect(self._Scanned);self.Scanner.Failed.connect(self._Failed);self.Scanner.start()
    def _Progress(self,value:int,status:str)->None:self.Progress.setValue(value);self.Status.setText(status)
    def _Scanned(self,metadata:dict)->None:
        self.Progress.setValue(90);self.Status.setText("Loading asset database and scene…")
        if not self.Runtime.LoadProject(self.Project):self._Failed(self.Runtime.LastError());return
        self.Progress.setValue(100);self.Status.setText("Ready");self.Loaded.emit(metadata)
    def _Failed(self,message:str)->None:QMessageBox.critical(self,"Project Could Not Be Loaded",message);QApplication.instance().quit()

def _EditorSettings()->SettingsStore:
    return SettingsStore("editor",1,lambda:{"schema_version":1,"theme":"dark","recent_scene":""},{0:lambda value:{"theme":value.get("theme","dark"),"recent_scene":""}})

def main(arguments:list[str]|None=None)->int:
    parser=argparse.ArgumentParser(prog="BAZZALT Editor");parser.add_argument("--project",required=True,type=Path);parser.add_argument("--editor-version",default="1.0.0");options=parser.parse_args(arguments)
    app=QApplication(sys.argv if arguments is None else [sys.argv[0],*arguments]);app.setApplicationName("BAZZALT Editor");app.setOrganizationName("BAZZALT")
    try:version=Version.Parse(options.editor_version)
    except ValueError as error:QMessageBox.critical(None,"Invalid Editor Version",str(error));return 2
    store=_EditorSettings();settings=store.Load();resources=ResourceManager();localization=LocalizationManager(resources);themes=ThemeManager(app,Theme.light() if settings.get("theme")=="light" else Theme.dark());runtime=RuntimeService();state={}
    loading=LoadingWindow(options.project.resolve(),version,runtime);state["loading"]=loading
    def OpenEditor(metadata:dict)->None:
        window=Editor(themes,resources,localization,runtime);window.Controller._ProjectLoaded(str(options.project.resolve()));window.Controller.RefreshHierarchy();window.setWindowTitle(f"{metadata['name']} — BAZZALT {version}");window.showMaximized();loading.close();state["editor"]=window
    loading.Loaded.connect(OpenEditor);loading.show();result=app.exec();settings["theme"]="light" if themes.GetTheme().background==Theme.light().background else "dark";store.Save(settings);return result

if __name__=="__main__":raise SystemExit(main())
