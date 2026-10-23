"""Asynchronous external game compiler integration. No engine build is performed."""
import json
import sys
from pathlib import Path
from PySide6.QtCore import QObject,QProcess,QProcessEnvironment
from .gui.build_settings import BuildSettingsDialog
from .gui.build_progress import BuildProgressDialog,BuildCompleteDialog
from Compiler.settings import Load
from .gui.panels.console import ConsoleLevel

class GameBuildController(QObject):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Process=None;self.Progress=None;self.Completion=None;self._buffer=''
        editor.MenuBar.BuildSettingsRequested.connect(self.Settings);editor.MenuBar.BuildRequested.connect(lambda:self.Build(False));editor.MenuBar.BuildLaunchRequested.connect(lambda:self.Build(True))
    def Settings(self):
        try:
            if self.Editor.Runtime.ProjectInfo().get('project_path'):BuildSettingsDialog(self.Editor).exec()
        except (OSError,ValueError,KeyError) as error:self._Message(str(error),True)
    def _Message(self,text,error=False):self.Editor.Console.AddMessage(text,ConsoleLevel.Error if error else ConsoleLevel.Info,True,self.Editor.Localization.Translate('menu.build'))
    def Build(self,launch):
        tr=self.Editor.Localization.Translate
        if self.Process is not None:return
        if self.Editor.Runtime.IsPlaying():self._Message(tr('build.stop_first'),True);return
        if self.Editor.Controller.IsDirty:
            self._Message(tr('build.save_first'),True);return
        try:
            project=Path(self.Editor.Runtime.ProjectInfo()['project_path']);settings=Load(project);arch=settings['target']['architecture'];root=Path(__file__).resolve().parents[1];app=Path(sys.executable).parent
            runtime=Path(settings['target']['runtime_directory']) if settings['target']['runtime_directory'] else app/'RuntimeTemplates'/('windows-'+arch)
            if not runtime.is_dir():runtime=root/'build/RuntimeTemplates'/('windows-'+arch) if not settings['target']['runtime_directory'] else runtime
            tool=app/'tools/compiler/BazzaltCompiler.exe';cooker=app/'tools/compiler/BazzaltCooker.exe'
            if tool.is_file():program=str(tool);arguments=[]
            else:
                if getattr(sys,'frozen',False) or '__compiled__' in globals():raise ValueError(tr('build.tools_missing'))
                program=sys.executable;arguments=['-m','Compiler.build'];cooker=root/'build/Editor/Release/BazzaltCooker.exe'
            arguments+=['--project',str(project),'--runtime',str(runtime),'--cooker',str(cooker)]
            compiler=self.Editor.PreferenceValue('tools','compiler_path','')
            if compiler:arguments+=['--script-compiler',compiler]
            if launch:arguments+=['--launch']
            process=QProcess(self);self.Process=process;self._buffer='';self._stderr='';self._error='';self._result_path='';self._launch_requested=launch;self._launch_error='';self._cancelled=False
            process.setProgram(program);process.setArguments(arguments);process.setWorkingDirectory(str(root));environment=QProcessEnvironment.systemEnvironment();environment.insert('PYTHONUTF8','1');environment.insert('PYTHONIOENCODING','utf-8');process.setProcessEnvironment(environment)
            process.readyReadStandardOutput.connect(self._Output);process.readyReadStandardError.connect(self._Stderr);process.finished.connect(self._Finished);process.errorOccurred.connect(self._Failed)
            for action in (self.Editor.MenuBar.BuildAction,self.Editor.MenuBar.BuildLaunchAction):action.setEnabled(False)
            self.Progress=BuildProgressDialog(self.Editor);self.Progress.CancelRequested.connect(self.Cancel);self.Progress.show();process.start()
        except (OSError,ValueError,KeyError) as error:self._Message(str(error),True)
    def _Output(self):
        if self.Process is None:return
        self._buffer+=bytes(self.Process.readAllStandardOutput()).decode('utf-8','replace')
        while '\n' in self._buffer:
            line,self._buffer=self._buffer.split('\n',1)
            try:
                event=json.loads(line);stage=event.get('stage','')
                if stage=='error':self._error=event.get('message','')
                elif stage=='cancelled':self._cancelled=True
                elif stage=='launch_error':self._launch_error=event.get('message','')
                else:
                    if self.Progress is not None:self.Progress.SetStage(stage)
                    if stage=='complete':self._result_path=event.get('output','')
            except (ValueError,TypeError):
                if line.strip():self._stderr+=line+'\n'
    def _Stderr(self):
        if self.Process is not None:self._stderr+=bytes(self.Process.readAllStandardError()).decode('utf-8','replace')
    def _Failed(self,error):
        if self.Process is not None:self._error=self.Process.errorString()
        if error==QProcess.ProcessError.FailedToStart:self._Finished(-1,None)
    def Cancel(self):
        if self.Process is not None:self.Process.write(b'cancel\n')
    def _Finished(self,code,status):
        if self.Process is None:return
        self._Output();self._Stderr()
        self.Process.deleteLater();self.Process=None
        for action in (self.Editor.MenuBar.BuildAction,self.Editor.MenuBar.BuildLaunchAction):action.setEnabled(True)
        if getattr(self,'_cancelled',False) and code==2 and status!=QProcess.ExitStatus.CrashExit:
            self._cancelled=False
            if self.Progress is not None:self.Progress.Finish()
            self._Message(self.Editor.Localization.Translate('build.cancelled'));return
        if code or status==QProcess.ExitStatus.CrashExit or self._error or not self._result_path:
            message=self._error or self._stderr or self.Editor.Localization.Translate('build.failed')
            if self.Progress is not None:self.Progress.Fail(message)
            self._Message(message,True);return
        if self.Progress is not None:self.Progress.Finish()
        if self._launch_requested:
            self._Message(self.Editor.Localization.Translate('build.stage.complete')+' '+self._result_path)
            if self._launch_error:self._Message(self._launch_error,True)
        else:
            self.Completion=BuildCompleteDialog(self.Editor,Path(self._result_path));self.Completion.show()
    def IsRunning(self):return self.Process is not None
