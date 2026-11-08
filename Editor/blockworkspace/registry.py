"""BlockRegistry: stable type-id -> BlockDefinition, categories, search."""
from __future__ import annotations
from typing import Optional
from PySide6.QtCore import QObject, Signal

from .definition import BlockDefinition
from .valuetypes import ValueTypeRegistry


class CategoryInfo:
    def __init__(self, Name, Color=None, Icon=None, Order=None):
        self.Name, self.Color, self.Icon, self.Order = Name, Color, Icon, Order


class BlockRegistry(QObject):
    Changed = Signal()

    def __init__(self):
        super().__init__()
        self._defs: dict[str, BlockDefinition] = {}
        self._cats: dict[str, CategoryInfo] = {}
        self.ValueTypes = ValueTypeRegistry()
        self._batch = 0
        self._dirty = False

    def Register(self, block_type) -> BlockDefinition:
        d = BlockDefinition.FromType(block_type)
        self._defs[d.TypeId] = d
        if d.Category not in self._cats:
            self._cats[d.Category] = CategoryInfo(d.Category)
        self._Changed()
        return d

    def RegisterMany(self, types):
        self._batch += 1
        try:
            return [self.Register(t) for t in types]
        finally:
            self._batch -= 1
            if self._batch == 0 and self._dirty:
                self._dirty = False
                self.Changed.emit()

    def _Changed(self):
        if self._batch:
            self._dirty = True
        else:
            self.Changed.emit()

    def Unregister(self, type_id: str):
        if self._defs.pop(type_id, None) is not None:
            self._Changed()

    def Get(self, type_id: str) -> Optional[BlockDefinition]:
        return self._defs.get(type_id)

    def Contains(self, type_id: str) -> bool:
        return type_id in self._defs

    def All(self) -> list:
        return list(self._defs.values())

    def AddCategory(self, Name, Color=None, Icon=None, Order=None) -> CategoryInfo:
        c = self._cats.get(Name) or CategoryInfo(Name)
        if Color is not None:
            c.Color = Color
        if Icon is not None:
            c.Icon = Icon
        if Order is not None:
            c.Order = Order
        self._cats[Name] = c
        self._Changed()
        return c

    def Category(self, Name) -> Optional[CategoryInfo]:
        return self._cats.get(Name)

    def Categories(self) -> list:
        used = {d.Category for d in self._defs.values()}
        cats = [c for n, c in self._cats.items() if n in used]
        idx = {c.Name: i for i, c in enumerate(cats)}
        cats.sort(key=lambda c: (c.Order if c.Order is not None else 1000 + idx[c.Name]))
        return cats

    def InCategory(self, name: str) -> list:
        return [d for d in self._defs.values() if d.Category == name]

    def Search(self, text: str) -> list:
        toks = [t for t in text.lower().split() if t]
        if not toks:
            return self.All()
        out = []
        for d in self._defs.values():
            hay = " ".join([d.DisplayName, d.TypeId, d.Category] + list(d.Keywords)).lower()
            if all(t in hay for t in toks):
                out.append(d)
        return out
