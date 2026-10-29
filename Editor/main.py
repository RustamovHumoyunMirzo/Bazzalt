from __future__ import annotations
import argparse, os, sys, traceback, subprocess
from pathlib import Path
if sys.platform.startswith("linux"):os.environ.setdefault("QT_QPA_PLATFORM","xcb")
from PySide6.QtCore import QObject, QTimer, Qt, QThread, Signal, Slot, QEvent, QEventLoop
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication, QDialog, QLabel, QMessageBox, QProgressBar, QVBoxLayout

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
            if not files:self.Progress.emit(85,"")
            for index,path in enumerate(files,1):self.Progress.emit(min(85,index*85//len(files)),path.name)
            self.Completed.emit(metadata)
        except (OSError,ValueError) as error:self.Failed.emit(str(error))

class StartupInputBlocker(QObject):
    """Discard initialization input rather than replaying it in the ready editor."""
    Inputs = frozenset(getattr(QEvent.Type,name) for name in (
        'MouseButtonPress','MouseButtonRelease','MouseButtonDblClick','MouseMove',
        'KeyPress','KeyRelease','Shortcut','ShortcutOverride','Wheel','ContextMenu',
        'TouchBegin','TouchUpdate','TouchEnd','TabletPress','TabletMove','TabletRelease',
        'InputMethod','DragEnter','DragMove','Drop'))
    def eventFilter(self,watched,event):
        if event.type() in self.Inputs:event.accept();return True
        return False

class LoadingWindow(QDialog):
    Loaded=Signal(object)
    def __init__(self,project:Path,version:Version,runtime:RuntimeService,localization:LocalizationManager)->None:
        super().__init__();self.Project=project;self.Runtime=runtime;self.Tr=localization.Translate
        self.setAttribute(Qt.WidgetAttribute.WA_QuitOnClose,False);self.setWindowTitle(self.Tr('loading.title'));self.setFixedSize(480,130)
        self.setWindowFlags(Qt.WindowType.Dialog|Qt.WindowType.CustomizeWindowHint|Qt.WindowType.WindowTitleHint)
        self.setWindowModality(Qt.WindowModality.ApplicationModal)
        self._centerTimer=QTimer(self);self._centerTimer.setSingleShot(True);self._centerTimer.timeout.connect(self._Center)
        layout=QVBoxLayout(self);self.Title=QLabel(project.stem);self.Status=QLabel(self.Tr('loading.validating'));self.Progress=QProgressBar();self.Progress.setRange(0,100)
        layout.addWidget(self.Title);layout.addWidget(self.Status);layout.addWidget(self.Progress)
        self.Scanner=ProjectScanner(project,version);self.Scanner.Progress.connect(self._Progress);self.Scanner.Completed.connect(self._Scanned);self.Scanner.Failed.connect(self._Failed);self._started=False
    def Start(self)->None:
        if self._started:return
        self._started=True;self.Scanner.start()
    def _Center(self)->None:
        if not self.isVisible():return
        owner=self.parentWidget()
        bounds=owner.frameGeometry() if owner is not None else self.screen().availableGeometry()
        frame=self.frameGeometry();frame.moveCenter(bounds.center());self.move(frame.topLeft())
    def showEvent(self,event)->None:
        super().showEvent(event);self._Center();self._centerTimer.start(0)
    def eventFilter(self,watched,event):
        if watched is self.parentWidget() and event.type() in (QEvent.Type.Move,QEvent.Type.Resize,QEvent.Type.Show,QEvent.Type.WindowStateChange):
            self._centerTimer.start(0)
        return False
    def _Progress(self,value:int,status:str)->None:self.Progress.setValue(value);self.Status.setText(status or self.Tr('loading.scanning_empty'))
    def _Scanned(self,metadata:dict)->None:
        self.Progress.setValue(90);self.Status.setText(self.Tr('loading.assets'));self.repaint()
        try:
            if not self.Runtime.LoadProject(self.Project):self._Failed(self.Runtime.LastError());return
        except Exception as error:
            self._Failed(self.Tr('loading.native_failed',error=error));return
        self.Progress.setRange(0,0);self.Status.setText(self.Tr('loading.editor'));self.repaint()
        QTimer.singleShot(0, lambda: self.Loaded.emit(metadata))
    def reject(self)->None:pass  # Escape must not expose an incompletely initialized editor.
    def closeEvent(self,event)->None:event.ignore()
    def _Failed(self,message:str)->None:QMessageBox.critical(self,self.Tr('loading.project_failed'),message);QApplication.instance().quit()

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
        self.Loading=LoadingWindow(project,version,self.Runtime,self.Localization)
        self._startupInput=None;self._readyFrames=0;self._finishing=False;self._startupFailed=False
        self.Loading.Loaded.connect(self.OpenEditor)
    def Start(self)->None:self.Loading.show();self.Loading.Start()
    @Slot(object)
    def OpenEditor(self,metadata:dict)->None:
        try:
            self._startupInput=StartupInputBlocker(self);self.App.installEventFilter(self._startupInput)
            window=Editor(self.Themes,self.Resources,self.Localization,self.Runtime,self.Settings,self.SettingsSaver)
            self.EditorWindow=window;window.setEnabled(False)
            # Editor policies install their own application filters during
            # construction. Keep the startup blocker ahead of those policies.
            self.App.installEventFilter(self._startupInput)
            window.Controller.FrameCompleted.connect(self._StartupFrame)
            window.Controller.FrameFailed.connect(self._StartupFailed)
            for surface in (window.Scene.Surface,window.Output.Surface):surface.AttachmentFailed.connect(self._StartupFailed)
            window.Controller._ProjectLoaded(str(self.Project));window.Controller.RefreshHierarchy()
            window.setWindowTitle(f"{metadata['name']} — BAZZALT {self.Version}")
            # Establish ownership before showing the disabled editor. Native
            # render surfaces still attach and render underneath this dialog.
            self.Loading.setParent(window,self.Loading.windowFlags())
            window.installEventFilter(self.Loading)
            self.Loading.setWindowModality(Qt.WindowModality.ApplicationModal)
            window.showMaximized();self.Loading.show();self.Loading.raise_();self.Loading.repaint()
            self.Loading.Status.setText(self.Localization.Translate('loading.viewports'))
            if not self.Runtime.IsAvailable():self._StartupFailed(self.Runtime.LastError())
        except Exception:
            self._StartupFailed(traceback.format_exc())

    def _StartupFrame(self)->None:
        if self._finishing:return
        # Hidden tabs attach when opened later; do not wait for their surfaces.
        native=QGuiApplication.platformName().casefold() not in {'offscreen','minimal','minimalegl'}
        surfaces=(self.EditorWindow.Scene.Surface,self.EditorWindow.Output.Surface)
        if native and any(surface.isVisible() and not surface._attached for surface in surfaces):return
        self._readyFrames+=1
        if self._readyFrames<2:return
        self._finishing=True;QTimer.singleShot(0,self._FinishStartup)

    def _ReleaseStartupInput(self)->None:
        if self._startupInput is not None:
            self.App.removeEventFilter(self._startupInput);self._startupInput.deleteLater();self._startupInput=None

    def _FinishStartup(self)->None:
        if self._startupFailed:return
        window=self.EditorWindow;timer=window.Controller.Timer;running=timer.isActive();timer.stop()
        # Process queued native/Qt input while the dialog, disabled window and
        # input filter still block it. Do not run a nested render tick here.
        self.App.processEvents(QEventLoop.ProcessEventsFlag.AllEvents)
        # Nested platform event pumping need not deliver every posted event.
        # Flush input explicitly through the still-installed blocker before
        # enabling widgets. Leave timers, signals and deletion events alone.
        for kind in StartupInputBlocker.Inputs:self.App.sendPostedEvents(None,int(kind))
        if self._startupFailed:return
        self._ReleaseStartupInput()
        window.Controller.FrameCompleted.disconnect(self._StartupFrame)
        window.Controller.FrameFailed.disconnect(self._StartupFailed)
        for surface in (window.Scene.Surface,window.Output.Surface):surface.AttachmentFailed.disconnect(self._StartupFailed)
        self.Loading.hide();self.Loading.deleteLater();window.setEnabled(True)
        if running:timer.start()
        window.raise_();window.activateWindow();self.App.setQuitOnLastWindowClosed(True)

    def _StartupFailed(self,details:str)->None:
        if self._startupFailed:return
        self._startupFailed=True;self._finishing=True;self._ReleaseStartupInput()
        details=details or self.Localization.Translate('loading.viewport_failed')
        if self.EditorWindow is not None:self.EditorWindow.Controller.Timer.stop()
        log=DataPaths.Logs()/"editor-startup.log"
        try:log.parent.mkdir(parents=True,exist_ok=True);log.write_text(details,encoding="utf-8")
        except OSError:pass
        QMessageBox.critical(self.Loading,self.Localization.Translate('loading.editor_failed'),details);self.App.quit()

def main(arguments:list[str]|None=None)->int:
    import struct
    if struct.calcsize("P") != 8:
        raise RuntimeError("BAZZALT Editor requires a 64-bit machine and Python runtime.")
    parser=argparse.ArgumentParser(prog="BAZZALT Editor");parser.add_argument("--project",type=Path);parser.add_argument("--editor-version",default="0.8.0");parser.add_argument("--check-runtime",action="store_true",help=argparse.SUPPRESS);options=parser.parse_args(arguments)
    if options.project is None and not options.check_runtime: parser.error("the following arguments are required: --project")
    app=QApplication(sys.argv if arguments is None else [sys.argv[0],*arguments]);app.setStyle("Fusion");app.setApplicationName("BAZZALT Editor");app.setOrganizationName("BAZZALT");app.setQuitOnLastWindowClosed(False)
    if options.check_runtime:
        runtime=RuntimeService();return 0 if runtime.IsAvailable() else 3
    from Editor.diagnostics import StartDiagnostics
    StartDiagnostics(DataPaths.Logs())
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
