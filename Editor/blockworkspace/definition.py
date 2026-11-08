"""BlockType (user-facing declaration) -> BlockDefinition (compiled, runtime description)."""
from __future__ import annotations
import re
from typing import Any, Callable, Optional

from .fields import InputDef, FieldInput, ValueInput, StatementInput, RawFieldInput

NEXT = "@Next"


class BlockShape:
    Statement = "Statement"
    Value = "Value"
    Boolean = "Boolean"
    Entry = "Entry"
    Container = "Container"


def SectionKey(section: str, uid: int, name: str) -> str:
    return f"{section}#{uid}/{name}"


class SectionDef:
    """A repeatable / optional block section (e.g. ELIF, ELSE). Its Inputs are instantiated per section instance."""

    def __init__(self, Name: str, Inputs: list, MenuLabel: Optional[str] = None, Min: int = 0,
                 Max: Optional[int] = None, Default: int = 0):
        self.Name = Name
        self.Inputs = list(Inputs)
        self.MenuLabel = MenuLabel or Name
        self.Min = Min
        self.Max = Max
        self.Default = Default


class BlockAction:
    """Context-menu entry. Callback signature: ``fn(Block, Workspace)``."""

    def __init__(self, Text: str, Callback: Optional[Callable] = None, Enabled: bool = True,
                 Checked: Optional[bool] = None, Separator: bool = False, Submenu: Optional[list] = None):
        self.Text, self.Callback, self.Enabled, self.Checked = Text, Callback, Enabled, Checked
        self.Separator, self.Submenu = Separator, Submenu


class BlockType:
    """Subclass to declare a block. Instances are stateless singletons; hooks receive the BlockModel."""
    Type: str = ""
    DisplayName: str = ""
    Category: str = "General"
    Color: Optional[str] = None
    Shape: Optional[str] = None
    OutputType: Optional[str] = None
    Inputs: list = []
    Sections: list = []
    TailInputs: list = []
    Icon: Any = None
    Tooltip: str = ""
    Keywords: list = []
    XmlTag: Optional[str] = None
    HasPrevious: Optional[bool] = None
    HasNext: Optional[bool] = None
    Generators: dict = {}          # {"xml": fn(ctx)}

    # ----- hooks ---------------------------------------------------------------------------
    def Validate(self, Block) -> list:
        """Return messages (str | (severity, str) | ValidationIssue)."""
        return []

    def ValidateConnection(self, Block, Slot: str, Child) -> Optional[str]:
        """Called on the *parent*. Return an error string to reject."""
        return None

    def ValidateParent(self, Block, Parent, Slot: str) -> Optional[str]:
        """Called on the *child*. Return an error string to reject."""
        return None

    def GetContextActions(self, Block) -> list:
        return []

    def OnCreated(self, Block):
        pass

    def OnFieldChanged(self, Block, Key: str, Old, New):
        pass

    def OnSectionsChanged(self, Block):
        pass

    def Summary(self, Block) -> str:
        """Text shown when the block is collapsed."""
        parts = []
        for ri in Block.Inputs():
            if ri.Kind == "field" and len(parts) < 2:
                parts.append(ri.Def.DisplayText(Block.Values.get(ri.Key)))
        return (self.DisplayName or self.Type) + (": " + ", ".join(p for p in parts if p) if parts else "")

    def BuildShape(self, Item):
        """Return a QPainterPath to override the default shape, or None."""
        return None

    def PaintDecoration(self, Painter, Item, Theme):
        """Draw extra visuals on top of the default rendering."""

    def SerializeMetadata(self, Block) -> dict:
        return dict(Block.Metadata)

    def DeserializeMetadata(self, Block, Data: dict):
        Block.Metadata.update(Data)


def _kebab(s: str) -> str:
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1-\2", s)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", s)
    return s.lower()


class ResolvedInput:
    __slots__ = ("Key", "Def", "Section", "Uid")

    def __init__(self, Key, Def, Section=None, Uid=None):
        self.Key, self.Def, self.Section, self.Uid = Key, Def, Section, Uid

    @property
    def Kind(self):
        return self.Def.Kind


class BlockDefinition:
    def __init__(self):
        self.TypeId = ""
        self.DisplayName = ""
        self.Category = "General"
        self.Color: Optional[str] = None
        self.Shape = BlockShape.Statement
        self.OutputType: Optional[str] = None
        self.Inputs: list = []
        self.TailInputs: list = []
        self.SectionDefs: dict[str, SectionDef] = {}
        self.SectionOrder: list[str] = []
        self.Icon = None
        self.Tooltip = ""
        self.Keywords: list = []
        self.XmlTag: Optional[str] = None
        self.HasPrevious = True
        self.HasNext = True
        self.Instance: BlockType = BlockType()
        self.Generators: dict = {}
        self.IsUnknown = False
        self._unknown_inputs: list = []

    # ---- derived
    @property
    def IsValue(self):
        return self.Shape in (BlockShape.Value, BlockShape.Boolean)

    @property
    def HasSections(self):
        return bool(self.SectionDefs)

    @property
    def XmlName(self):
        return self.XmlTag or _kebab(self.TypeId.split(".")[-1])

    @classmethod
    def FromType(cls, t) -> "BlockDefinition":
        inst = t() if isinstance(t, type) else t
        if not inst.Type:
            raise ValueError(f"{type(inst).__name__}.Type must be a non-empty stable identifier")
        d = cls()
        d.Instance = inst
        d.TypeId = inst.Type
        d.DisplayName = inst.DisplayName or inst.Type.split(".")[-1]
        d.Category = inst.Category or "General"
        d.Color = inst.Color
        d.Icon = inst.Icon
        d.Tooltip = inst.Tooltip
        d.Keywords = list(inst.Keywords or [])
        d.XmlTag = inst.XmlTag
        d.Generators = dict(inst.Generators or {})
        d.Inputs = list(inst.Inputs or [])
        d.TailInputs = list(inst.TailInputs or [])
        for s in inst.Sections or []:
            d.SectionDefs[s.Name] = s
            d.SectionOrder.append(s.Name)
        # validate names
        def check(inputs, where):
            seen = set()
            for i in inputs:
                if i.Kind == "label":
                    continue
                if not i.Name or i.Name in seen:
                    raise ValueError(f"{d.TypeId}: duplicate/empty input name {i.Name!r} in {where}")
                seen.add(i.Name)
        check(d.Inputs + d.TailInputs, "block")
        for s in d.SectionDefs.values():
            check(s.Inputs, f"section {s.Name}")
        has_stmt = any(i.Kind == "statement" for i in d.Inputs + d.TailInputs) or \
            any(i.Kind == "statement" for s in d.SectionDefs.values() for i in s.Inputs)
        shape = inst.Shape
        if shape is None:
            if inst.OutputType == "Boolean":
                shape = BlockShape.Boolean
            elif inst.OutputType:
                shape = BlockShape.Value
            elif has_stmt:
                shape = BlockShape.Container
            else:
                shape = BlockShape.Statement
        if shape == BlockShape.Statement and has_stmt:
            shape = BlockShape.Container
        d.Shape = shape
        if shape == BlockShape.Boolean:
            d.OutputType = inst.OutputType or "Boolean"
        elif shape == BlockShape.Value:
            d.OutputType = inst.OutputType or "Any"
        d.HasPrevious = shape in (BlockShape.Statement, BlockShape.Container)
        d.HasNext = shape in (BlockShape.Statement, BlockShape.Container, BlockShape.Entry)
        if inst.HasPrevious is not None:
            d.HasPrevious = bool(inst.HasPrevious) and not d.IsValue
        if inst.HasNext is not None:
            d.HasNext = bool(inst.HasNext) and not d.IsValue
        return d

    @classmethod
    def Unknown(cls, type_id: str, raw: dict) -> "BlockDefinition":
        d = cls()
        d.IsUnknown = True
        d.TypeId = type_id
        d.DisplayName = f"Unknown: {type_id}"
        d.Category = "Unknown"
        d.Color = "#9AA0A6"
        shape = raw.get("Shape") or BlockShape.Statement
        if shape not in (BlockShape.Statement, BlockShape.Value, BlockShape.Boolean, BlockShape.Entry, BlockShape.Container):
            shape = BlockShape.Statement
        d.Shape = shape
        if shape in (BlockShape.Value, BlockShape.Boolean):
            d.OutputType = raw.get("Output") or ("Boolean" if shape == BlockShape.Boolean else "Any")
        d.HasPrevious = shape in (BlockShape.Statement, BlockShape.Container)
        d.HasNext = shape in (BlockShape.Statement, BlockShape.Container, BlockShape.Entry)
        for name in raw.get("Fields", []):
            d._unknown_inputs.append(ResolvedInput(name, RawFieldInput(name, ""), None, None))
        for name, kind in raw.get("Slots", []):
            inp = StatementInput(name) if kind == "statement" else ValueInput(name, ("Any",))
            d._unknown_inputs.append(ResolvedInput(name, inp, None, None))
        d.Instance = BlockType()
        return d

    # ---- input resolution
    def ResolveInputs(self, sections) -> list:
        if self.IsUnknown:
            return list(self._unknown_inputs)
        out = [ResolvedInput(i.Name, i) for i in self.Inputs]
        for name, uid in sections:
            sd = self.SectionDefs.get(name)
            if sd is None:
                continue
            for i in sd.Inputs:
                out.append(ResolvedInput(SectionKey(name, uid, i.Name), i, name, uid))
        out += [ResolvedInput(i.Name, i) for i in self.TailInputs]
        return out

    def ColorFor(self, theme) -> str:
        return self.Color or theme.CategoryColor(self.Category)
