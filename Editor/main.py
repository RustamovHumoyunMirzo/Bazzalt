from __future__ import annotations
import argparse, os, sys, traceback, subprocess
from pathlib import Path
from PySide6.QtCore import QObject, QTimer, Qt, QThread, Signal, Slot
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
from bazzalt.settings import DataPaths, ReadProjectMetadata, SettingsStore, Version
from bazzalt.branding import LogoIcon
from Editor.gui.preferences import MergePreferences

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
        super().__init__();self.Project=project;self.Runtime=runtime;self.setAttribute(Qt.WidgetAttribute.WA_QuitOnClose,False);self.setWindowTitle("Loading BAZZALT Project");self.setFixedSize(480,130)
        layout=QVBoxLayout(self);self.Title=QLabel(project.stem);self.Status=QLabel("Validating project…");self.Progress=QProgressBar();self.Progress.setRange(0,100)
        layout.addWidget(self.Title);layout.addWidget(self.Status);layout.addWidget(self.Progress)
        self.Scanner=ProjectScanner(project,version);self.Scanner.Progress.connect(self._Progress);self.Scanner.Completed.connect(self._Scanned);self.Scanner.Failed.connect(self._Failed);self._started=False
    def Start(self)->None:
        if self._started:return
        self._started=True;self.Scanner.start()
    def _Progress(self,value:int,status:str)->None:self.Progress.setValue(value);self.Status.setText(status)
    def _Scanned(self,metadata:dict)->None:
        self.Progress.setValue(90);self.Status.setText("Loading asset database and scene…")
        try:
            if not self.Runtime.LoadProject(self.Project):self._Failed(self.Runtime.LastError());return
        except Exception as error:
            self._Failed(f"Native project loading failed: {error}");return
        self.Progress.setValue(100);self.Status.setText("Opening editor…")
        QTimer.singleShot(0, lambda: self.Loaded.emit(metadata))
    def _Failed(self,message:str)->None:QMessageBox.critical(self,"Project Could Not Be Loaded",message);QApplication.instance().quit()

def _EditorSettings()->SettingsStore:
    defaults=lambda:{"schema_version":3,"theme":"dark","recent_scene":"","panel_layout":None,"preferences":None}
    return SettingsStore("editor",3,defaults,{0:lambda value:{"theme":value.get("theme","dark"),"recent_scene":""},1:lambda value:{**value,"panel_layout":None},2:lambda value:{**value,"preferences":{"appearance":{"theme":value.get("theme","dark")}}}})

class EditorSession(QObject):
    """Owns the complete loader-to-editor transition for the process lifetime."""
    def __init__(self,app:QApplication,project:Path,version:Version,settings:dict,settings_saver=None)->None:
        super().__init__();self.App=app;self.Project=project;self.Version=version;self.EditorWindow=None;self.Settings=settings;self.SettingsSaver=settings_saver
        self.Resources=ResourceManager();self.Localization=LocalizationManager(self.Resources)
        self.Themes=ThemeManager(app,Theme.light() if settings.get("theme")=="light" else Theme.dark())
        app.setWindowIcon(LogoIcon("#202020" if settings.get("theme")=="light" else "#eeeeee"))
        self.Runtime=RuntimeService()
        backend=MergePreferences(settings.get("preferences"))["rendering"]["backend"]
        if backend not in self.Runtime.SupportedRenderingBackends():backend="automatic"
        self.Runtime.ConfigureRenderingBackend(backend)
        self.Loading=LoadingWindow(project,version,self.Runtime)
        self.Loading.Loaded.connect(self.OpenEditor)
    def Start(self)->None:self.Loading.show();self.Loading.Start()
    @Slot(object)
    def OpenEditor(self,metadata:dict)->None:
        try:
            window=Editor(self.Themes,self.Resources,self.Localization,self.Runtime,self.Settings,self.SettingsSaver)
            window.Controller._ProjectLoaded(str(self.Project));window.Controller.RefreshHierarchy()
            window.setWindowTitle(f"{metadata['name']} — BAZZALT {self.Version}")
            self.EditorWindow=window;window.showMaximized();window.raise_();window.activateWindow()
            self.Loading.hide();self.Loading.deleteLater()
            QTimer.singleShot(0,lambda:self.App.setQuitOnLastWindowClosed(True))
        except Exception:
            details=traceback.format_exc();log=DataPaths.Logs()/"editor-startup.log"
            try:log.parent.mkdir(parents=True,exist_ok=True);log.write_text(details,encoding="utf-8")
            except OSError:pass
            QMessageBox.critical(self.Loading,"Editor Could Not Be Opened",details);self.App.quit()

def main(arguments:list[str]|None=None)->int:
    parser=argparse.ArgumentParser(prog="BAZZALT Editor");parser.add_argument("--project",type=Path);parser.add_argument("--editor-version",default="1.0.0");parser.add_argument("--check-runtime",action="store_true",help=argparse.SUPPRESS);options=parser.parse_args(arguments)
    if options.project is None and not options.check_runtime: parser.error("the following arguments are required: --project")
    app=QApplication(sys.argv if arguments is None else [sys.argv[0],*arguments]);app.setStyle("Fusion");app.setApplicationName("BAZZALT Editor");app.setOrganizationName("BAZZALT");app.setQuitOnLastWindowClosed(False)
    if options.check_runtime:
        runtime=RuntimeService();return 0 if runtime.IsAvailable() else 3
    try:version=Version.Parse(options.editor_version)
    except ValueError as error:QMessageBox.critical(None,"Invalid Editor Version",str(error));return 2
    store=_EditorSettings();settings=store.Load();session=EditorSession(app,options.project.resolve(),version,settings,store.Save)
    session.Start();result=app.exec();settings["theme"]="light" if session.Themes.GetTheme().background==Theme.light().background else "dark";store.Save(settings)
    if result==75:
        # Release native scene locks and swap chains before the replacement
        # process opens the same project. Preserve the hub's project/version.
        session.Runtime.Release()
        command=RestartCommand(options.project.resolve(),str(version))
        try:
            subprocess.Popen(command,cwd=Path(__file__).resolve().parent.parent,creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
        except OSError as error:
            QMessageBox.critical(None,session.Localization.Translate("restart.title"),session.Localization.Translate("restart.failed")+" "+str(error))
            return 1
        return 0
    return result

def RestartCommand(project:Path,version:str)->list[str]:
    arguments=["--project",str(project),"--editor-version",version]
    if getattr(sys,"frozen",False) or "__compiled__" in globals():return [sys.executable,*arguments]
    return [sys.executable,"-m","Editor.main",*arguments]

if __name__=="__main__":raise SystemExit(main())
