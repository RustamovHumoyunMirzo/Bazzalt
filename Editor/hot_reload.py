"""Debounced, background gameplay compilation. Runtime swaps occur on the UI thread."""
from pathlib import Path
from PySide6.QtCore import QObject,QFileSystemWatcher,QThread,QTimer,Signal,Slot
from .scripting import BuildResult,Diagnostic

class _Compile(QThread):
    Completed=Signal(object)
    def __init__(self,compiler,sources,parent):
        super().__init__(parent);self.Compiler=compiler;self.Sources=sources
    def run(self):
        try:result=self.Compiler.Build(self.Sources)
        except Exception as error:result=BuildResult(False,diagnostics=(Diagnostic('error',str(error)),))
        self.Completed.emit(result)

class HotReloadController(QObject):
    def __init__(self,controller):
        super().__init__(controller);self.Controller=controller;self.Worker=None;self.Sources=[];self.Pending=False;self.Generation=0;self.Stamps={};self.Active=False
        self.Watcher=QFileSystemWatcher(self);self.Watcher.fileChanged.connect(self._Changed);self.Watcher.directoryChanged.connect(self._Changed)
        self.Timer=QTimer(self);self.Timer.setSingleShot(True);self.Timer.setInterval(400);self.Timer.timeout.connect(self.Reload)
    def Track(self):
        self.Active=True
        c=self.Controller
        sources=list(c.ScriptAttachments.UsedSources()) if c.ScriptAttachments else []
        sources.extend(row["source"] for row in c.Runtime.SceneScripts())
        sources=list(dict.fromkeys([*sources,*c.Prefabs.NativeSources()]))
        self.Sources=[Path(path) for path in sources if Path(path).suffix.lower() in ('.cpp','.lua')]
        paths=set(self.Sources)
        for path in self.Sources:
            if path.suffix.lower()=='.cpp':
                try:paths.update(c.ScriptCompiler._ValidateIncludes(path))
                except (OSError,ValueError):pass
        self.Stamps={str(path):self._Stamp(path) for path in paths}
        paths.update(path.parent for path in tuple(paths))
        old=self.Watcher.files()+self.Watcher.directories()
        if old:self.Watcher.removePaths(old)
        available=[str(path) for path in paths if path.exists()]
        if available:self.Watcher.addPaths(available)
    def Stop(self):
        self.Active=False;self.Generation+=1;self.Timer.stop();self.Pending=False
        paths=self.Watcher.files()+self.Watcher.directories()
        if paths:self.Watcher.removePaths(paths)
    @staticmethod
    def _Stamp(path):
        try:stat=Path(path).stat();return (stat.st_mtime_ns,stat.st_size)
        except OSError:return None
    def Shutdown(self):
        self.Stop()
        if self.Worker:self.Worker.wait()
    def _Changed(self,*_):
        if not self.Active:return
        changed=any(self._Stamp(path)!=stamp for path,stamp in self.Stamps.items())
        if changed and self.Controller.Window.PreferenceValue("scripting","hot_reload",True) and self.Controller.Runtime.IsPlaying():self.Pending=True;self.Timer.start()
    def Reload(self):
        c=self.Controller
        if not c.Runtime.IsPlaying() or not c.ScriptCompiler:return
        if self.Worker is not None:self.Pending=True;return
        self.Pending=False;self.Track();self._compile_generation=self.Generation
        sources=[str(path) for path in self.Sources if path.suffix.lower()=='.cpp']
        self._building_sources=sources
        self.Worker=_Compile(c.ScriptCompiler,sources,self)
        self.Worker.Completed.connect(self._Completed)
        self.Worker.finished.connect(self._Finished)
        self.Worker.start()
    def _Finished(self):
        worker=self.Worker;self.Worker=None
        if worker:worker.deleteLater()
        if self.Pending:self.Timer.start()
    @Slot(object)
    def _Completed(self,result):
        generation=self._compile_generation
        c=self.Controller
        if generation!=self.Generation or not c.Runtime.IsPlaying() or self.Pending:return
        from .gui.panels.console import ConsoleLevel
        tr=c.Window.Localization.Translate
        def log(key,level=ConsoleLevel.Info,**args):c.Window.Console.AddMessage(tr(key,**args),level,True,tr('scripting.runtime_source'))
        for diagnostic in result.diagnostics:
            c.Window.Console.AddMessage(tr(diagnostic.localization_key) if diagnostic.localization_key else diagnostic.message,ConsoleLevel.Error if diagnostic.level=='error' else ConsoleLevel.Warning if diagnostic.level=='warning' else ConsoleLevel.Info,True,tr('scripting.compiler_source'))
        if not result.success:log('scripting.reload_failed',ConsoleLevel.Error);return
        lua=[str(path) for path in self.Sources if path.suffix.lower()=='.lua']
        if lua and not c.Runtime.RefreshLuaAssets(lua):log('scripting.reload_error',ConsoleLevel.Error,error=c.Runtime.LastError());return
        try:
            # RuntimeBindings uses attachment source order; map explicitly because
            # prefab-only sources also participate in this build.
            modules={str(Path(source).resolve()):str(output) for source,output in zip(self._building_sources,result.outputs)}
            bindings=[]
            records=[]
            for entity,entries in c.ScriptAttachments.values.get('entities',{}).items():
                records.extend(dict(entry,entity=entity) for entry in entries)
            records.extend(c.Runtime.SceneScripts())
            seen=set()
            for record in records:
                if not record.get('enabled',True):continue
                key=(record['entity'],record['type'])
                if key in seen:continue
                seen.add(key);source=Path(record['source']).resolve()
                module=str(source) if source.suffix.lower()=='.lua' else modules.get(str(source))
                if not module:continue
                descriptor=c.ScriptCompiler.Inspect(source)
                names={prop.name for prop in descriptor.properties} if descriptor else set()
                properties={name:value for name,value in record.get('properties',{}).items() if name in names}
                bindings.append(dict(entity=record['entity'],type=record['type'],module=module,properties=properties))
            bindings=[binding for binding in bindings if not binding['entity'].startswith('@')]
            if not c.Runtime.ReloadScripts(bindings):log('scripting.reload_error',ConsoleLevel.Error,error=c.Runtime.LastError());return
            c.Prefabs.RegisterModules(self._building_sources,result.outputs)
            log('scripting.reload_success')
        except (OSError,ValueError,RuntimeError) as error:log('scripting.reload_error',ConsoleLevel.Error,error=str(error))
