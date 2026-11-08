"""BlockModel (pure data) and BlockDocument (owns models, transactions, signals)."""
from __future__ import annotations
import uuid
from contextlib import contextmanager
from typing import Any, Callable, Iterable, Optional

from PySide6.QtCore import QObject, Signal

from .definition import BlockDefinition, NEXT, BlockShape
from .commands import BlockCommandStack


def NewBlockId() -> str:
    return "b" + uuid.uuid4().hex[:10]


class BlockModel:
    __slots__ = ("Id", "Definition", "X", "Y", "Values", "Sections", "NextUid", "Comment", "CommentOpen",
                 "Disabled", "Collapsed", "Metadata", "Parent", "ParentSlot", "Children", "Raw", "_inputs", "_imap")

    def __init__(self, Id: str, Definition: BlockDefinition):
        self.Id = Id
        self.Definition = Definition
        self.X = 0.0
        self.Y = 0.0
        self.Values: dict = {}
        self.Sections: list = []
        self.NextUid = 1
        self.Comment = ""
        self.CommentOpen = False
        self.Disabled = False
        self.Collapsed = False
        self.Metadata: dict = {}
        self.Parent: Optional[BlockModel] = None
        self.ParentSlot: Optional[str] = None
        self.Children: dict = {}
        self.Raw = None
        self._inputs = None
        self._imap = None

    def __repr__(self):
        return f"<Block {self.Definition.TypeId} {self.Id}>"

    # ---- basic info
    @property
    def Type(self):
        return self.Definition.TypeId

    @property
    def IsUnknown(self):
        return self.Definition.IsUnknown

    def InvalidateInputs(self):
        self._inputs = None
        self._imap = None

    def Inputs(self) -> list:
        if self._inputs is None:
            self._inputs = self.Definition.ResolveInputs(self.Sections)
        return self._inputs

    def InputByKey(self, key: str):
        if self._imap is None:
            self._imap = {ri.Key: ri for ri in self.Inputs()}
        return self._imap.get(key)

    def Get(self, key: str, default=None):
        return self.Values.get(key, default)

    def SectionUids(self, name: str) -> list:
        return [u for n, u in self.Sections if n == name]

    # ---- links
    @property
    def Next(self):
        return self.Children.get(NEXT)

    @property
    def Previous(self):
        return self.Parent if self.ParentSlot == NEXT else None

    def Chain(self):
        b = self
        while b is not None:
            yield b
            b = b.Children.get(NEXT)

    def LastInChain(self):
        b = self
        while True:
            n = b.Children.get(NEXT)
            if n is None:
                return b
            b = n

    def StackHead(self):
        b = self
        while b.Parent is not None and b.ParentSlot == NEXT:
            b = b.Parent
        return b

    def Root(self):
        b = self
        while b.Parent is not None:
            b = b.Parent
        return b

    def Ancestors(self):
        b = self.Parent
        while b is not None:
            yield b
            b = b.Parent

    def Walk(self, IncludeNext: bool = True):
        """Iterative pre-order traversal of this block and everything attached below."""
        stack = [self]
        while stack:
            b = stack.pop()
            yield b
            for slot, c in reversed(list(b.Children.items())):
                if slot == NEXT and not IncludeNext and b is self:
                    continue
                stack.append(c)

    def Descendants(self):
        it = self.Walk()
        next(it)
        return it

    def IsEffectivelyDisabled(self) -> bool:
        b = self
        while b is not None:
            if b.Disabled:
                return True
            b = b.StackHead().Parent   # followers inherit only from the enclosing container
        return False

    # ---- state capture
    def CaptureState(self) -> dict:
        return {
            "Type": self.Definition.TypeId, "X": self.X, "Y": self.Y,
            "Values": dict(self.Values), "Sections": list(self.Sections), "NextUid": self.NextUid,
            "Comment": self.Comment, "CommentOpen": self.CommentOpen, "Disabled": self.Disabled,
            "Collapsed": self.Collapsed, "Metadata": dict(self.Metadata),
            "Parent": self.Parent.Id if self.Parent else None, "ParentSlot": self.ParentSlot,
            "Children": {s: c.Id for s, c in self.Children.items()}, "Raw": self.Raw,
        }

    def ApplyScalarState(self, st: dict):
        self.X, self.Y = st["X"], st["Y"]
        self.Values = dict(st["Values"])
        self.Sections = list(st["Sections"])
        self.NextUid = st["NextUid"]
        self.Comment, self.CommentOpen = st["Comment"], st["CommentOpen"]
        self.Disabled, self.Collapsed = st["Disabled"], st["Collapsed"]
        self.Metadata = dict(st["Metadata"])
        self.InvalidateInputs()


class BlockDocument(QObject):
    BlockAdded = Signal(object)
    BlockRemoved = Signal(object)
    BlockChanged = Signal(object, str)
    Changed = Signal()                 # DocumentChanged
    Updated = Signal(object, object)   # internal: (changed ids, removed ids) for graphics sync

    def __init__(self, Registry):
        super().__init__()
        self.Registry = Registry
        self.Blocks: dict[str, BlockModel] = {}
        self.Roots: dict[str, BlockModel] = {}
        self.Commands = BlockCommandStack(self)
        self.Connections = None        # set by workspace (BlockConnectionManager)
        self.PositionProvider: Optional[Callable] = None
        self._dirty: set = set()
        self._removed: set = set()

    # ----------------------------------------------------------------- transactions / flush
    @contextmanager
    def Transaction(self, Name: str = "Edit", MergeKey=None):
        self.Commands.Begin(Name, MergeKey)
        try:
            yield self
        except BaseException:
            self.Commands.Rollback()
            self.Flush()
            raise
        else:
            self.Commands.End()
            if self.Commands.Depth == 0:
                self.Flush()

    def Flush(self):
        if not self._dirty and not self._removed:
            return
        d, r = self._dirty, self._removed
        self._dirty, self._removed = set(), set()
        self.Updated.emit(d, r)
        self.Changed.emit()

    def _Touch(self, b: BlockModel):
        self.Commands.Touch(b)
        self._dirty.add(b.Id)

    # ----------------------------------------------------------------- queries
    def Find(self, Id: str) -> Optional[BlockModel]:
        return self.Blocks.get(Id)

    def SortedRoots(self) -> list:
        return sorted(self.Roots.values(), key=lambda b: (b.Y, b.X))

    def ScenePos(self, b: BlockModel):
        if self.PositionProvider is not None:
            p = self.PositionProvider(b)
            if p is not None:
                return p
        r = b.Root()
        n = 0
        x = b
        while x.Parent is not None:
            n += 1
            x = x.Parent
        return (r.X + 20 * n, r.Y + 30 * n)

    # ----------------------------------------------------------------- creation
    def _InitDefaults(self, b: BlockModel):
        for ri in b.Inputs():
            self._InitInputDefault(b, ri)

    def _InitInputDefault(self, b, ri):
        d = ri.Def
        if d.Kind == "field":
            b.Values[ri.Key] = d.GetDefault()
        elif d.Kind == "value" and d.Shadow is not None:
            b.Values[ri.Key] = d.Shadow.GetDefault()

    def _NewBlock(self, TypeId: str, Id: Optional[str] = None, Definition=None) -> BlockModel:
        d = Definition or self.Registry.Get(TypeId)
        if d is None:
            raise KeyError(f"Block type {TypeId!r} is not registered")
        bid = Id if Id and Id not in self.Blocks else NewBlockId()
        b = BlockModel(bid, d)
        self.Commands.TouchNew(b)
        self.Blocks[bid] = b
        self.Roots[bid] = b
        self._dirty.add(bid)
        if not d.IsUnknown:
            for sd in d.SectionDefs.values():
                for _ in range(sd.Default):
                    b.Sections.append((sd.Name, b.NextUid))
                    b.NextUid += 1
            b.InvalidateInputs()
            self._InitDefaults(b)
        return b

    def CreateBlock(self, TypeId: str, X: float = 0.0, Y: float = 0.0, Values: Optional[dict] = None, Id: Optional[str] = None) -> BlockModel:
        with self.Transaction("Add Block"):
            b = self._NewBlock(TypeId, Id)
            b.X, b.Y = float(X), float(Y)
            if Values:
                for k, v in Values.items():
                    ri = b.InputByKey(k)
                    b.Values[k] = self._CoerceFor(ri, v)
            b.Definition.Instance.OnCreated(b)
            self.BlockAdded.emit(b)
        return b

    @staticmethod
    def _CoerceFor(ri, v):
        if ri is None:
            return v
        d = ri.Def
        if d.Kind == "field":
            return d.Coerce(v)
        if d.Kind == "value" and d.Shadow is not None:
            return d.Shadow.Coerce(v)
        return v

    # ----------------------------------------------------------------- primitive link ops
    def _Link(self, parent: BlockModel, slot: str, child: BlockModel):
        self._Touch(parent)
        self._Touch(child)
        parent.Children[slot] = child
        child.Parent, child.ParentSlot = parent, slot
        self.Roots.pop(child.Id, None)

    def _Unlink(self, child: BlockModel):
        p = child.Parent
        if p is None:
            return
        self._Touch(p)
        self._Touch(child)
        p.Children.pop(child.ParentSlot, None)
        child.Parent, child.ParentSlot = None, None
        self.Roots[child.Id] = child

    def _SetPos(self, b: BlockModel, x: float, y: float):
        self._Touch(b)
        b.X, b.Y = float(x), float(y)

    # ----------------------------------------------------------------- public editing ops
    def SetPosition(self, b: BlockModel, x: float, y: float):
        with self.Transaction("Move Block", MergeKey=("move", b.Id)):
            self._SetPos(b, x, y)
            self.BlockChanged.emit(b, "Position")

    def SetFieldValue(self, b: BlockModel, key: str, value) -> bool:
        ri = b.InputByKey(key)
        v = self._CoerceFor(ri, value)
        old = b.Values.get(key)
        if old == v and key in b.Values:
            return False
        with self.Transaction("Edit Field"):
            self._Touch(b)
            b.Values[key] = v
            b.Definition.Instance.OnFieldChanged(b, key, old, v)
            self.BlockChanged.emit(b, "Field")
        return True

    def SetComment(self, b, text: str, Open: Optional[bool] = None):
        with self.Transaction("Edit Comment"):
            self._Touch(b)
            b.Comment = text or ""
            b.CommentOpen = bool(b.Comment) if Open is None else bool(Open)
            self.BlockChanged.emit(b, "Comment")

    def SetCommentOpen(self, b, Open: bool):
        with self.Transaction("Toggle Comment"):
            self._Touch(b)
            b.CommentOpen = bool(Open)
            self.BlockChanged.emit(b, "Comment")

    def SetDisabled(self, b, value: bool):
        with self.Transaction("Enable/Disable Block"):
            self._Touch(b)
            b.Disabled = bool(value)
            self.BlockChanged.emit(b, "Disabled")

    def SetCollapsed(self, b, value: bool):
        with self.Transaction("Collapse/Expand Block"):
            self._Touch(b)
            b.Collapsed = bool(value)
            self.BlockChanged.emit(b, "Collapsed")

    def SetMetadata(self, b, key: str, value):
        with self.Transaction("Edit Metadata"):
            self._Touch(b)
            if value is None:
                b.Metadata.pop(key, None)
            else:
                b.Metadata[key] = value
            self.BlockChanged.emit(b, "Metadata")

    def Disconnect(self, b: BlockModel, Offset=(28.0, 22.0)):
        """Detach ``b`` (with everything attached below/inside it) from its parent."""
        if b.Parent is None:
            return
        with self.Transaction("Disconnect Block"):
            x, y = self.ScenePos(b)
            self._Unlink(b)
            self._SetPos(b, x + Offset[0], y + Offset[1])
            self.BlockChanged.emit(b, "Structure")

    def DetachForDrag(self, b: BlockModel, HealStack: bool = False):
        """Detach for dragging, keeping the block at its current scene position. With HealStack only this block
        leaves the stack (its followers re-attach to the previous block)."""
        with self.Transaction("Drag Block"):
            x, y = self.ScenePos(b)
            nxt = b.Next if HealStack else None
            parent, slot = b.Parent, b.ParentSlot
            if nxt is not None:
                self._Unlink(nxt)
            if parent is not None:
                self._Unlink(b)
            self._SetPos(b, x, y)
            if nxt is not None:
                if parent is not None:
                    self._Link(parent, slot, nxt)
                else:
                    self._SetPos(nxt, x, y + 40)
            self.BlockChanged.emit(b, "Structure")

    # ---- removal
    def RemoveBlocks(self, blocks: Iterable[BlockModel], Heal: bool = True):
        todo = []
        seen = set()
        for b in blocks:
            if b.Id in self.Blocks and b.Id not in seen:
                seen.add(b.Id)
                todo.append(b)
        if not todo:
            return
        with self.Transaction("Delete Blocks"):
            for b in todo:
                if b.Id in self.Blocks:
                    self._RemoveOne(b, Heal)

    def _RemoveOne(self, b: BlockModel, Heal: bool):
        parent, slot = b.Parent, b.ParentSlot
        nxt = b.Children.get(NEXT) if Heal else None
        pos = self.ScenePos(b)
        if nxt is not None:
            self._Unlink(nxt)
        if parent is not None:
            self._Unlink(b)
        if nxt is not None:
            healed = False
            if parent is not None and self.Connections is not None:
                healed = self.Connections.CanConnect(parent, slot, nxt).Ok
            if healed:
                self._Link(parent, slot, nxt)
            else:
                self._SetPos(nxt, pos[0], pos[1])
        for x in list(b.Walk()):
            self._DeleteRaw(x)

    def _DeleteRaw(self, x: BlockModel):
        self._Touch(x)
        self.Blocks.pop(x.Id, None)
        self.Roots.pop(x.Id, None)
        self._removed.add(x.Id)
        self._dirty.discard(x.Id)
        self.BlockRemoved.emit(x)

    def Clear(self):
        if not self.Blocks:
            return
        with self.Transaction("Clear Workspace"):
            for b in list(self.Roots.values()):
                self._RemoveOne(b, False)

    # ---- sections (dynamic / mutator blocks)
    def SectionCount(self, b, name: str) -> int:
        return len(b.SectionUids(name))

    def CanAddSection(self, b, name: str) -> bool:
        sd = b.Definition.SectionDefs.get(name)
        return sd is not None and (sd.Max is None or self.SectionCount(b, name) < sd.Max)

    def CanRemoveSection(self, b, name: str) -> bool:
        sd = b.Definition.SectionDefs.get(name)
        return sd is not None and self.SectionCount(b, name) > sd.Min

    def _DefaultSectionIndex(self, b, name):
        order = b.Definition.SectionOrder
        my = order.index(name)
        last_same = -1
        first_greater = None
        for i, (n, _) in enumerate(b.Sections):
            oi = order.index(n) if n in order else 0
            if n == name:
                last_same = i
            elif oi > my and first_greater is None:
                first_greater = i
        if last_same >= 0:
            return last_same + 1
        return first_greater if first_greater is not None else len(b.Sections)

    def AddSection(self, b: BlockModel, name: str, Index: Optional[int] = None) -> Optional[int]:
        if not self.CanAddSection(b, name):
            return None
        with self.Transaction("Add " + b.Definition.SectionDefs[name].MenuLabel):
            self._Touch(b)
            uid = b.NextUid
            b.NextUid += 1
            idx = self._DefaultSectionIndex(b, name) if Index is None else max(0, min(Index, len(b.Sections)))
            b.Sections.insert(idx, (name, uid))
            b.InvalidateInputs()
            for ri in b.Inputs():
                if ri.Uid == uid:
                    self._InitInputDefault(b, ri)
            b.Definition.Instance.OnSectionsChanged(b)
            self.BlockChanged.emit(b, "Sections")
        return uid

    def RemoveSection(self, b: BlockModel, uid: int) -> bool:
        entry = next(((n, u) for n, u in b.Sections if u == uid), None)
        if entry is None or not self.CanRemoveSection(b, entry[0]):
            return False
        with self.Transaction("Remove " + b.Definition.SectionDefs[entry[0]].MenuLabel):
            self._Touch(b)
            for ri in list(b.Inputs()):
                if ri.Uid == uid:
                    child = b.Children.get(ri.Key)
                    if child is not None:
                        x, y = self.ScenePos(child)
                        self._Unlink(child)
                        self._SetPos(child, x + 40, y + 24)
                    b.Values.pop(ri.Key, None)
            b.Sections.remove(entry)
            b.InvalidateInputs()
            b.Definition.Instance.OnSectionsChanged(b)
            self.BlockChanged.emit(b, "Sections")
        return True

    # ----------------------------------------------------------------- undo / redo application
    def _BlockFromState(self, bid: str, st: dict) -> BlockModel:
        raw = st.get("Raw")
        d = None if raw is not None else self.Registry.Get(st["Type"])
        if d is None:
            d = BlockDefinition.Unknown(st["Type"], raw or {})
        b = BlockModel(bid, d)
        b.Raw = raw
        return b

    def ApplyStates(self, states: dict):
        cmds = self.Commands
        cmds.Applying = True
        try:
            removed, created = [], []
            for bid, st in states.items():
                blk = self.Blocks.get(bid)
                if st is None:
                    if blk is not None:
                        removed.append(blk)
                elif blk is None:
                    blk = self._BlockFromState(bid, st)
                    self.Blocks[bid] = blk
                    created.append(blk)
            for bid, st in states.items():
                if st is not None:
                    self.Blocks[bid].ApplyScalarState(st)
            for bid, st in states.items():
                if st is None:
                    continue
                blk = self.Blocks[bid]
                blk.Parent = self.Blocks.get(st["Parent"]) if st["Parent"] else None
                blk.ParentSlot = st["ParentSlot"] if blk.Parent is not None else None
                blk.Children = {s: self.Blocks[c] for s, c in st["Children"].items() if c in self.Blocks}
            for blk in removed:
                self.Blocks.pop(blk.Id, None)
                self.Roots.pop(blk.Id, None)
                self._removed.add(blk.Id)
            for bid, st in states.items():
                blk = self.Blocks.get(bid)
                if blk is None:
                    continue
                self._dirty.add(bid)
                if blk.Parent is None:
                    self.Roots[bid] = blk
                else:
                    self.Roots.pop(bid, None)
            for blk in removed:
                self.BlockRemoved.emit(blk)
            for blk in created:
                self.BlockAdded.emit(blk)
            for bid, st in states.items():
                blk = self.Blocks.get(bid)
                if blk is not None and blk not in created:
                    self.BlockChanged.emit(blk, "Undo")
        finally:
            cmds.Applying = False
