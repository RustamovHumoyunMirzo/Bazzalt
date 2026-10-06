"""Debounced imports for attached Lua sources; never scan the model database."""
from pathlib import Path
from PySide6.QtCore import QObject,QFileSystemWatcher,QTimer,Signal

class LuaAssetWatcher(QObject):
    Finished=Signal()
    Failed=Signal(str)
    def __init__(self,runtime,parent=None):
        super().__init__(parent);self.Runtime=runtime;self._sources=set();self._pending=set()
        self._watcher=QFileSystemWatcher(self);self._watcher.fileChanged.connect(self._Changed)
        self._timer=QTimer(self);self._timer.setSingleShot(True);self._timer.setInterval(120);self._timer.timeout.connect(self._Import)
    def Track(self,sources):
        self._sources={str(Path(path).resolve()) for path in sources if Path(path).suffix.lower()==".lua"}
        self._pending.intersection_update(self._sources)
        watched=set(self._watcher.files());removed=watched-self._sources
        if removed:self._watcher.removePaths(list(removed))
        added={path for path in self._sources-watched if Path(path).is_file()}
        if added:self._watcher.addPaths(list(added))
    def IsBusy(self):return bool(self._pending)
    def _Changed(self,path):
        if path in self._sources:self._pending.add(path);self._timer.start()
    def _Import(self):
        if self.Runtime.IsPlaying():self._timer.start(200);return
        sources=list(self._pending);self._pending.clear()
        # Atomic saves replace the file inode and remove its old watcher.
        self.Track(self._sources)
        if sources and not self.Runtime.RefreshLuaAssets(sources):self.Failed.emit(self.Runtime.LastError())
        else:self.Finished.emit()
