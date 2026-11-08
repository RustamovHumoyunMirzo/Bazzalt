"""BlockConnectionManager: typed connection validation and connect/insert logic."""
from __future__ import annotations
from typing import Callable, Optional

from .definition import NEXT


class ConnectionResult:
    def __init__(self, Ok: bool, Reason: str = ""):
        self.Ok, self.Reason = Ok, Reason

    def __bool__(self):
        return self.Ok

    def __repr__(self):
        return f"ConnectionResult({self.Ok}, {self.Reason!r})"


OK = ConnectionResult(True)


class BlockConnectionManager:
    def __init__(self, Document, Registry):
        self.Document = Document
        self.Registry = Registry
        self._validators: list[Callable] = []
        Document.Connections = self

    def AddValidator(self, fn: Callable):
        """fn(parent, slot, child) -> Optional[str] (error message to reject)."""
        self._validators.append(fn)

    def _Fail(self, why):
        return ConnectionResult(False, why)

    def CanConnect(self, parent, slot: str, child) -> ConnectionResult:
        if parent is None or child is None:
            return self._Fail("missing block")
        if parent is child:
            return self._Fail("a block cannot connect to itself")
        # cycle: parent must not live inside child's subtree
        p = parent
        while p is not None:
            if p is child:
                return self._Fail("connection would create a cycle")
            p = p.Parent
        pd, cd = parent.Definition, child.Definition
        if slot == NEXT:
            if not pd.HasNext:
                return self._Fail(f"'{pd.DisplayName}' has no next connection")
            if not cd.HasPrevious:
                return self._Fail(f"'{cd.DisplayName}' has no previous connection")
        else:
            ri = parent.InputByKey(slot)
            if ri is None:
                return self._Fail(f"unknown slot '{slot}'")
            if ri.Kind == "value":
                if not cd.IsValue:
                    return self._Fail("statement blocks cannot plug into a value socket")
                if not self.Registry.ValueTypes.IsAssignable(cd.OutputType, ri.Def.Accepts):
                    return self._Fail(f"type '{cd.OutputType}' is not accepted (expects {', '.join(ri.Def.Accepts)})")
            elif ri.Kind == "statement":
                if not cd.HasPrevious:
                    return self._Fail(f"'{cd.DisplayName}' cannot be placed in a statement slot")
            else:
                return self._Fail("slot does not accept blocks")
        msg = pd.Instance.ValidateConnection(parent, slot, child)
        if msg:
            return self._Fail(msg)
        msg = cd.Instance.ValidateParent(child, parent, slot)
        if msg:
            return self._Fail(msg)
        for v in self._validators:
            msg = v(parent, slot, child)
            if msg:
                return self._Fail(str(msg))
        return OK

    def OpenSlots(self, block) -> list:
        """(slot, kind) for every connection point of ``block`` that currently accepts a child."""
        out = []
        for ri in block.Inputs():
            if ri.Kind in ("value", "statement") and ri.Key not in block.Children:
                out.append((ri.Key, ri.Kind))
        if block.Definition.HasNext and NEXT not in block.Children:
            out.append((NEXT, "next"))
        return out

    def Connect(self, parent, slot: str, child, Bump=(30.0, 26.0)) -> bool:
        res = self.CanConnect(parent, slot, child)
        if not res.Ok:
            return False
        doc = self.Document
        with doc.Transaction("Connect Blocks"):
            if child.Parent is not None:
                x, y = doc.ScenePos(child)
                doc._Unlink(child)
                doc._SetPos(child, x, y)
            old = parent.Children.get(slot)
            pos = None
            if old is not None:
                pos = doc.ScenePos(old)
                doc._Unlink(old)
            doc._Link(parent, slot, child)
            if old is not None:
                placed = False
                ri = parent.InputByKey(slot) if slot != NEXT else None
                if slot == NEXT or (ri is not None and ri.Kind == "statement"):
                    tail = child.LastInChain()
                    if tail.Definition.HasNext and self.CanConnect(tail, NEXT, old).Ok:
                        doc._Link(tail, NEXT, old)
                        placed = True
                if not placed:
                    doc._SetPos(old, pos[0] + Bump[0], pos[1] + Bump[1])
            doc.BlockChanged.emit(child, "Structure")
            doc.BlockChanged.emit(parent, "Structure")
        return True

    def Disconnect(self, block):
        self.Document.Disconnect(block)
