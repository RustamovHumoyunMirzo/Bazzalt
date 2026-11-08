"""Value type system for typed block connections."""
from __future__ import annotations
from typing import Iterable, Optional


class ValueType:
    def __init__(self, Name: str, Parent: Optional[str] = "Any", Color: Optional[str] = None, Description: str = ""):
        self.Name = Name
        self.Parent = None if Name == "Any" else (Parent or "Any")
        self.Color = Color
        self.Description = Description

    def __repr__(self):
        return f"ValueType({self.Name!r}, Parent={self.Parent!r})"


class ValueTypeRegistry:
    """Hierarchical value types. A socket accepting 'Asset' accepts any output type derived from it."""

    def __init__(self):
        self._types: dict[str, ValueType] = {}
        self._coercions: set[tuple[str, str]] = set()
        self.Register("Any", None, "#8A8A8A")
        self.Register("Number", "Any", "#59C059")
        self.Register("Integer", "Number", "#59C059")
        self.Register("Float", "Number", "#59C059")
        self.Register("String", "Any", "#CF63CF")
        self.Register("Boolean", "Any", "#5CB1D6")
        self.Register("Vector", "Any", "#4C97FF")
        self.Register("Color", "Any", "#FF6680")
        self.Register("Entity", "Any", "#FFAB19")
        self.Register("Asset", "Any", "#9966FF")
        self.AddCoercion("Integer", "Float")

    def Register(self, Name, Parent: Optional[str] = "Any", Color: Optional[str] = None, Description: str = "") -> ValueType:
        if isinstance(Name, ValueType):
            vt = Name
        else:
            vt = ValueType(Name, Parent, Color, Description)
        self._types[vt.Name] = vt
        return vt

    def AddCoercion(self, From: str, To: str):
        self._coercions.add((From, To))

    def Get(self, Name: str) -> ValueType:
        vt = self._types.get(Name)
        if vt is None:  # forward-declared / unknown types hang off Any
            vt = self.Register(Name, "Any")
        return vt

    def Ancestors(self, Name: str) -> list[str]:
        out, seen, cur = [], set(), Name
        while cur is not None and cur not in seen:
            seen.add(cur)
            out.append(cur)
            cur = self.Get(cur).Parent
        if "Any" not in out:
            out.append("Any")
        return out

    def IsAssignable(self, OutputType: Optional[str], Accepts: Optional[Iterable[str]]) -> bool:
        accepts = list(Accepts or [])
        if not accepts:
            return True
        out = OutputType or "Any"
        anc = self.Ancestors(out)
        for a in accepts:
            if a == "Any" or out == "Any" or a in anc or (out, a) in self._coercions:
                return True
        return False

    def ColorOf(self, Name: str) -> Optional[str]:
        return self.Get(Name).Color

    def Names(self) -> list[str]:
        return list(self._types)
