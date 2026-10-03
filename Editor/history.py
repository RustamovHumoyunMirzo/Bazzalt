"""Bounded scene-snapshot undo/redo history for editor authoring operations."""
from __future__ import annotations
from dataclasses import dataclass
from PySide6.QtCore import QObject,Signal

@dataclass(slots=True)
class SceneCommand:
    Label:str
    Before:bytes
    After:bytes

class SceneHistory(QObject):
    Changed=Signal()
    def __init__(self,runtime,parent=None,limit:int=100,byte_limit:int=128*1024*1024)->None:
        super().__init__(parent);self.Runtime=runtime;self.Limit=limit;self.ByteLimit=byte_limit;self._undo=[];self._redo=[];self._pending=None;self._restoring=False
    def Begin(self,label:str)->None:
        if self._pending is None and not self._restoring:self._pending=(label,self.Runtime.CaptureScene())
    def Configure(self,limit:int,byte_limit:int)->None:
        self.Limit=max(1,int(limit));self.ByteLimit=max(1024,int(byte_limit));self._Trim();self.Changed.emit()
    def _Trim(self)->None:
        while self._undo or self._redo:
            commands=self._undo+self._redo
            if len(commands)<=self.Limit and sum(len(c.Before)+len(c.After) for c in commands)<=self.ByteLimit:break
            if self._undo:self._undo.pop(0)
            else:self._redo.pop(0)
    def Commit(self)->bool:
        if self._pending is None:return False
        label,before=self._pending;self._pending=None;after=self.Runtime.CaptureScene()
        if not before or not after or before==after:return False
        self._undo.append(SceneCommand(label,before,after));self._redo.clear()
        self._Trim()
        self.Changed.emit();return True
    def Cancel(self)->None:self._pending=None
    def Undo(self)->bool:
        if not self._undo:return False
        command=self._undo.pop();self._restoring=True
        try:ok=self.Runtime.RestoreScene(command.Before)
        finally:self._restoring=False
        if ok:self._redo.append(command);self.Changed.emit()
        else:self._undo.append(command)
        return ok
    def Redo(self)->bool:
        if not self._redo:return False
        command=self._redo.pop();self._restoring=True
        try:ok=self.Runtime.RestoreScene(command.After)
        finally:self._restoring=False
        if ok:self._undo.append(command);self.Changed.emit()
        else:self._redo.append(command)
        return ok
    def Clear(self)->None:self._undo.clear();self._redo.clear();self._pending=None;self.Changed.emit()
    def Checkpoint(self):return (list(self._undo),list(self._redo))
    def RestoreCheckpoint(self,checkpoint)->None:
        self._undo,self._redo=(list(values) for values in checkpoint);self._pending=None;self._Trim();self.Changed.emit()
    def CanUndo(self)->bool:return bool(self._undo)
    def CanRedo(self)->bool:return bool(self._redo)
    def UndoLabel(self)->str:return self._undo[-1].Label if self._undo else ""
    def RedoLabel(self)->str:return self._redo[-1].Label if self._redo else ""

__all__=["SceneCommand","SceneHistory"]
