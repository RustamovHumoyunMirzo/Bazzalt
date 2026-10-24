"""Watch only GUI assets used by the scene/inspector; never rescan the project per tick."""
from pathlib import Path
from PySide6.QtCore import QObject,QFileSystemWatcher,QTimer,Signal
from .font_assets import FONT_EXTENSIONS

class GuiAssetWatcher(QObject):
    Changed=Signal(object)
    Failed=Signal(str,str)
    def __init__(self,runtime,parent=None):
        super().__init__(parent);self.Runtime=runtime;self._selected=None;self._sources=set();self._pending=set()
        self.Watcher=QFileSystemWatcher(self);self.Watcher.fileChanged.connect(self._Changed)
        self.Timer=QTimer(self);self.Timer.setSingleShot(True);self.Timer.setInterval(120);self.Timer.timeout.connect(self._Import)
    def Select(self,path):
        self._selected=str(Path(path).resolve()) if path and Path(path).suffix.lower() in FONT_EXTENSIONS|{'.css'} else None
        self.Track()
    def Track(self):
        sources={str(Path(path).resolve()) for path in getattr(self.Runtime,'GuiAssetSources',lambda:[])()}
        if self._selected:sources.add(self._selected)
        sources={path for path in sources if Path(path).suffix.lower() in FONT_EXTENSIONS|{'.css'}}
        watched=set(self.Watcher.files());removed=watched-sources
        if removed:self.Watcher.removePaths(list(removed))
        added={path for path in sources-watched if Path(path).is_file()}
        if added:self.Watcher.addPaths(list(added))
        self._pending.intersection_update(sources);self._pending.update(added-self._sources);self._sources=sources
        if self._pending:self.Timer.start()
    def _Changed(self,path):
        if path in self._sources:self._pending.add(path);self.Timer.start()
    def _Import(self):
        pending=sorted(self._pending);self._pending.clear();self.Track()
        for path in pending:
            if not self.Runtime.RefreshAsset(path):self.Failed.emit(path,self.Runtime.LastError())
        if pending:self.Changed.emit(pending)
