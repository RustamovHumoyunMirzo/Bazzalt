"""BlockCommandStack: transaction-based undo/redo built on per-block state diffs."""
from __future__ import annotations
from contextlib import contextmanager
from typing import Optional

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QUndoStack, QUndoCommand

class _EditorCommand(QUndoCommand):
    def __init__(self, owner, command):
        super().__init__(command.Name); self.owner=owner; self.command=command; self.first=True
    def undo(self):
        self.command.Undo(self.owner.Document); self.owner.Document.Flush(); self.owner.Changed.emit()
    def redo(self):
        if self.first:self.first=False; return
        self.command.Redo(self.owner.Document); self.owner.Document.Flush(); self.owner.Changed.emit()
    def id(self):return 1 if self.command.MergeKey is not None else -1
    def mergeWith(self,other):
        return isinstance(other,_EditorCommand) and other.owner is self.owner and self.command.MergeWith(other.command)



class BlockCommand:
    Name = "Command"
    MergeKey = None

    def Undo(self, Document):
        raise NotImplementedError

    def Redo(self, Document):
        raise NotImplementedError

    def MergeWith(self, Other) -> bool:
        return False


class StateCommand(BlockCommand):
    """Stores before/after state dicts for every block touched by a transaction."""

    def __init__(self, Name, Before, After, MergeKey=None):
        self.Name, self.Before, self.After, self.MergeKey = Name, Before, After, MergeKey

    def Undo(self, Document):
        Document.ApplyStates(self.Before)

    def Redo(self, Document):
        Document.ApplyStates(self.After)

    def MergeWith(self, Other):
        if self.MergeKey is None or Other.MergeKey != self.MergeKey or not isinstance(Other, StateCommand):
            return False
        for k, v in Other.Before.items():
            self.Before.setdefault(k, v)
        self.After.update(Other.After)
        return True


class CallbackCommand(BlockCommand):
    def __init__(self, Name, UndoFn, RedoFn):
        self.Name, self._u, self._r = Name, UndoFn, RedoFn

    def Undo(self, Document):
        self._u()

    def Redo(self, Document):
        self._r()


class CompositeCommand(BlockCommand):
    def __init__(self, Name, Commands):
        self.Name, self.Commands = Name, list(Commands)

    def Undo(self, Document):
        for c in reversed(self.Commands):
            c.Undo(Document)

    def Redo(self, Document):
        for c in self.Commands:
            c.Redo(Document)


class _Tx:
    def __init__(self, name, merge):
        self.Name, self.MergeKey, self.Before = name, merge, {}


class BlockCommandStack(QObject):
    Changed = Signal()

    def __init__(self, Document=None, Limit: int = 500):
        super().__init__()
        self.Document = Document
        self.Limit = Limit
        self.History = QUndoStack(self)
        self.History.setUndoLimit(Limit)
        self.History.indexChanged.connect(self.Changed)
        self._tx: Optional[_Tx] = None
        self._depth = 0
        self.Applying = False
        self._suspended = 0

    # ---- recording (called by the document)
    @property
    def Recording(self):
        return self._tx is not None and not self.Applying and not self._suspended

    def Touch(self, block):
        if self.Recording and block.Id not in self._tx.Before:
            self._tx.Before[block.Id] = block.CaptureState()

    def TouchNew(self, block):
        if self.Recording and block.Id not in self._tx.Before:
            self._tx.Before[block.Id] = None

    # ---- transactions
    @property
    def Depth(self):
        return self._depth

    def Begin(self, Name="Edit", MergeKey=None):
        if self._depth == 0:
            self._tx = _Tx(Name, MergeKey)
        self._depth += 1

    def Rename(self, Name):
        if self._tx:
            self._tx.Name = Name

    def End(self):
        if self._depth == 0:
            return None
        self._depth -= 1
        if self._depth > 0:
            return None
        tx, self._tx = self._tx, None
        if self.Applying or self._suspended or tx is None:
            return None
        doc = self.Document
        before, after = {}, {}
        for bid, st in tx.Before.items():
            blk = doc.Blocks.get(bid)
            a = blk.CaptureState() if blk is not None else None
            if st != a:
                before[bid], after[bid] = st, a
        if not before:
            return None
        cmd = StateCommand(tx.Name, before, after, tx.MergeKey)
        self.Push(cmd)
        return cmd

    def Rollback(self):
        tx, self._tx = self._tx, None
        self._depth = 0
        if tx is None:
            return
        states = dict(tx.Before)
        self.Document.ApplyStates(states)

    @contextmanager
    def Suspended(self):
        self._suspended += 1
        try:
            yield
        finally:
            self._suspended -= 1

    # ---- stack
    def Push(self, cmd: BlockCommand):
        self.History.push(_EditorCommand(self, cmd))
        self.Changed.emit()

    def Undo(self):self.History.undo()
    def Redo(self):self.History.redo()
    def Clear(self):self.History.clear()
    @property
    def CanUndo(self):return self.History.canUndo()
    @property
    def CanRedo(self):return self.History.canRedo()
    @property
    def UndoText(self):return self.History.undoText()
    @property
    def RedoText(self):return self.History.redoText()
