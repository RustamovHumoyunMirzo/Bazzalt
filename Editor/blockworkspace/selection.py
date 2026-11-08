"""BlockSelectionManager."""
from __future__ import annotations
from typing import Iterable, Optional
from PySide6.QtCore import QObject, Signal

from .definition import NEXT


class BlockSelectionManager(QObject):
    Changed = Signal()

    def __init__(self, Document):
        super().__init__()
        self.Document = Document
        self._ids: dict = {}
        Document.BlockRemoved.connect(self._OnRemoved)

    def _OnRemoved(self, block):
        if self._ids.pop(block.Id, None) is not None:
            self.Changed.emit()

    def _Id(self, b):
        return b if isinstance(b, str) else b.Id

    def Selected(self) -> list:
        return [self.Document.Blocks[i] for i in self._ids if i in self.Document.Blocks]

    def SelectedIds(self) -> set:
        return set(self._ids)

    def Count(self):
        return len(self._ids)

    @property
    def Primary(self):
        for i in reversed(list(self._ids)):
            if i in self.Document.Blocks:
                return self.Document.Blocks[i]
        return None

    def IsSelected(self, b) -> bool:
        return self._Id(b) in self._ids

    def Set(self, blocks: Iterable):
        new = {self._Id(b): True for b in blocks if self._Id(b) in self.Document.Blocks}
        if list(new) != list(self._ids):
            self._ids = new
            self.Changed.emit()

    def Add(self, blocks: Iterable):
        ch = False
        for b in blocks:
            i = self._Id(b)
            if i in self.Document.Blocks and i not in self._ids:
                self._ids[i] = True
                ch = True
        if ch:
            self.Changed.emit()

    def Remove(self, blocks: Iterable):
        ch = False
        for b in blocks:
            if self._ids.pop(self._Id(b), None) is not None:
                ch = True
        if ch:
            self.Changed.emit()

    def Toggle(self, b):
        i = self._Id(b)
        if i in self._ids:
            del self._ids[i]
        elif i in self.Document.Blocks:
            self._ids[i] = True
        self.Changed.emit()

    def Clear(self):
        if self._ids:
            self._ids = {}
            self.Changed.emit()

    def SelectAll(self):
        self.Set(self.Document.Blocks.values())

    def DragRoots(self) -> list:
        """Selected blocks that have no selected ancestor (they carry everything below/inside them)."""
        sel = self._ids
        out = []
        for b in self.Selected():
            p = b.Parent
            ok = True
            while p is not None:
                if p.Id in sel:
                    ok = False
                    break
                p = p.Parent
            if ok:
                out.append(b)
        return out

    def CopyRoots(self) -> list:
        """Roots for copying: a block is covered by another selected block when reachable through input slots, or
        through Next links whose target is itself selected."""
        sel = self._ids
        out = []
        for b in self.Selected():
            cur, included = b, False
            while cur.Parent is not None:
                if cur.ParentSlot == NEXT and cur.Id not in sel:
                    break
                cur = cur.Parent
                if cur.Id in sel:
                    included = True
                    break
            if not included:
                out.append(b)
        return out
