"""Bounded scene-snapshot undo/redo history for editor authoring operations."""
from __future__ import annotations
from dataclasses import dataclass
from PySide6.QtCore import QObject,Signal

@dataclass(slots=True)
class SceneCommand:
    Label:str
    Before:bytes
    After:bytes
    Kind:str="scene"
    Files:tuple=()

class SceneHistory(QObject):
    Changed=Signal()
    def __init__(self,runtime,parent=None,limit:int=100,byte_limit:int=128*1024*1024)->None:
        super().__init__(parent);self.Runtime=runtime;self.Limit=limit;self.ByteLimit=byte_limit;self._undo=[];self._redo=[];self._pending=None;self._restoring=False
        self.LastRestoreStructural=True;self.LastCommitSceneChanged=False;self._files={}
    def Begin(self,label:str)->None:
        if self._pending is None and not self._restoring:self._pending=(label,self.Runtime.CaptureScene(),"scene",())
    def BeginTransforms(self,label:str,entities)->None:
        if self._pending is not None or self._restoring:return
        entities=tuple(dict.fromkeys(entities));before=self.Runtime.CaptureTransforms(entities)
        if before:self._pending=(label,before,"transforms",entities)
        else:self.Begin(label)
    def Configure(self,limit:int,byte_limit:int)->None:
        self.Limit=max(1,int(limit));self.ByteLimit=max(1024,int(byte_limit));self._Trim();self.Changed.emit()
    def _Trim(self)->None:
        while self._undo or self._redo:
            commands=self._undo+self._redo
            if len(commands)<=self.Limit and sum(len(c.Before)+len(c.After)+sum(len(before or b'')+len(after or b'') for _,before,after in c.Files) for c in commands)<=self.ByteLimit:break
            if self._undo:self._undo.pop(0)
            else:self._redo.pop(0)
    def RecordFile(self,path):
        from pathlib import Path
        path=Path(path).resolve()
        if self._pending is None or (path not in self._files and len(self._files)>=32):raise ValueError('Start an edit first; at most 32 files per command')
        if path not in self._files:
            if path.exists() and path.stat().st_size>8*1024*1024:raise ValueError('File exceeds editor undo limit')
            self._files[path]=path.read_bytes() if path.exists() else None
    @staticmethod
    def _WriteFile(path,data):
        import os,tempfile
        if data is None:path.unlink(missing_ok=True);return
        path.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as stream:stream.write(data);name=stream.name
        try:os.replace(name,path)
        finally:
            from pathlib import Path
            Path(name).unlink(missing_ok=True)
    def _ApplyFiles(self,files,undo):
        for path,before,after in files:
            expected=after if undo else before
            if (path.read_bytes() if path.exists() else None)!=expected:return False
        completed=[]
        try:
            for path,before,after in files:self._WriteFile(path,before if undo else after);completed.append((path,before,after))
            if files:self.Runtime.RefreshAssets()
            return True
        except OSError:
            for path,before,after in reversed(completed):self._WriteFile(path,after if undo else before)
            return False
    def Commit(self)->bool:
        self.LastCommitSceneChanged=False
        if self._pending is None:return False
        label,before,kind,entities=self._pending
        after=self.Runtime.CaptureTransforms(entities) if kind=="transforms" else self.Runtime.CaptureScene()
        files=tuple((path,prior,path.read_bytes() if path.exists() else None) for path,prior in self._files.items());self._pending=None;self._files={};files=tuple(row for row in files if row[1]!=row[2])
        if not before or not after or (before==after and not files):return False
        self.LastCommitSceneChanged=before!=after
        self._undo.append(SceneCommand(label,before,after,kind,files));self._redo.clear()
        self._Trim()
        self.Changed.emit();return True
    def Cancel(self)->None:
        for path,before in self._files.items():
            if (path.read_bytes() if path.exists() else None)!=before:self._WriteFile(path,before)
        if self._files:self.Runtime.RefreshAssets()
        self._files={};self._pending=None
    def Undo(self)->bool:
        if not self._undo:return False
        command=self._undo.pop();self._restoring=True
        try:
            ok=self._ApplyFiles(command.Files,True)
            if ok:
                ok=self.Runtime.RestoreTransforms(command.Before) if command.Kind=="transforms" else self.Runtime.RestoreScene(command.Before)
                if not ok:self._ApplyFiles(command.Files,False)
        finally:self._restoring=False
        if ok:self.LastRestoreStructural=command.Kind=="scene";self._redo.append(command);self.Changed.emit()
        else:self._undo.append(command)
        return ok
    def Redo(self)->bool:
        if not self._redo:return False
        command=self._redo.pop();self._restoring=True
        try:
            ok=self._ApplyFiles(command.Files,False)
            if ok:
                ok=self.Runtime.RestoreTransforms(command.After) if command.Kind=="transforms" else self.Runtime.RestoreScene(command.After)
                if not ok:self._ApplyFiles(command.Files,True)
        finally:self._restoring=False
        if ok:self.LastRestoreStructural=command.Kind=="scene";self._undo.append(command);self.Changed.emit()
        else:self._redo.append(command)
        return ok
    def Clear(self)->None:self._undo.clear();self._redo.clear();self._pending=None;self._files={};self.Changed.emit()
    def Checkpoint(self):return (list(self._undo),list(self._redo))
    def RestoreCheckpoint(self,checkpoint)->None:
        self._undo,self._redo=(list(values) for values in checkpoint);self._pending=None;self._files={};self._Trim();self.Changed.emit()
    def CanUndo(self)->bool:return bool(self._undo)
    def CanRedo(self)->bool:return bool(self._redo)
    def UndoLabel(self)->str:return self._undo[-1].Label if self._undo else ""
    def RedoLabel(self)->str:return self._redo[-1].Label if self._redo else ""

__all__=["SceneCommand","SceneHistory"]
