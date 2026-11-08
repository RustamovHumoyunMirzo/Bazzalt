"""Pluggable code generation: BlockCodeGenerator + CodeTarget implementations (XML / JSON / pseudo-code).

A *target* defines the fragment type and how fragments are combined. Per-block generators are looked up in this order:
  1. ``Generator.RegisterGenerator(target, type_id, fn)``
  2. ``BlockType.Generators[target]``
  3. ``CodeTarget.DefaultEmit``
Generator functions receive a :class:`CodeContext` and return a fragment, a list of fragments, or None.
"""
from __future__ import annotations
import json
import re
import xml.etree.ElementTree as ET
from typing import Any, Callable, Optional

from .definition import NEXT, _kebab


class CodeContext:
    def __init__(self, Generator, Target, Block, Issues):
        self.Generator, self.Target, self.Block, self.Issues = Generator, Target, Block, Issues

    def Field(self, name: str, default=None):
        return self.Block.Values.get(name, default)

    def Warn(self, msg: str):
        self.Issues.append(f"{self.Block.Definition.DisplayName} ({self.Block.Id}): {msg}")

    def Value(self, slot: str):
        """Fragment generated for the block plugged into a value socket (None if empty/disabled)."""
        child = self.Block.Children.get(slot)
        if child is None or child.IsEffectivelyDisabled():
            return None
        frags = self.Generator._Emit(self.Target, child, self.Issues)
        return frags[0] if frags else None

    def Statements(self, slot: str) -> list:
        """Fragments for the statement chain inside a mouth."""
        return self.Generator._Chain(self.Target, self.Block.Children.get(slot), self.Issues)

    def Sections(self, name: str) -> list:
        """Section instances of this block as [(uid, {InputName: key})]."""
        out = []
        for ri_name, uid in self.Block.Sections:
            if ri_name == name:
                keys = {ri.Def.Name: ri.Key for ri in self.Block.Inputs() if ri.Uid == uid}
                out.append((uid, keys))
        return out


class CodeTarget:
    Name = "base"
    Extension = ".txt"

    def DefaultEmit(self, ctx: CodeContext):
        raise NotImplementedError

    def Finish(self, fragments: list, issues: list) -> str:
        raise NotImplementedError


# -------------------------------------------------------------------------------------------- XML
class XmlBehaviorTarget(CodeTarget):
    Name = "xml"
    Extension = ".xml"

    def __init__(self, RootTag: str = "behavior", OmitDefaults: bool = True, Indent: str = "    "):
        self.RootTag, self.OmitDefaults, self.Indent = RootTag, OmitDefaults, Indent

    # helpers available to generator functions
    def Element(self, tag: str, **attrs) -> ET.Element:
        e = ET.Element(tag)
        for k, v in attrs.items():
            e.set(k.replace("_", "-"), str(v))
        return e

    def Wrap(self, tag: str, *fragments, **attrs) -> ET.Element:
        e = self.Element(tag, **attrs)
        for f in fragments:
            if f is None:
                continue
            if isinstance(f, (list, tuple)):
                for x in f:
                    if x is not None:
                        e.append(x)
            else:
                e.append(f)
        return e

    def _Attr(self, el, ri, value):
        d = ri.Def
        if self.OmitDefaults and value == d.GetDefault():
            return
        el.set(_kebab(d.Name), d.Serialize(value))

    def DefaultEmit(self, ctx):
        b = ctx.Block
        el = ET.Element(b.Definition.XmlName)
        section_els: dict = {}
        for ri in b.Inputs():
            target = el
            if ri.Section:
                key = (ri.Section, ri.Uid)
                if key not in section_els:
                    section_els[key] = ET.SubElement(el, _kebab(ri.Section))
                target = section_els[key]
            d = ri.Def
            if d.Kind == "field":
                if self.OmitDefaults and b.Values.get(ri.Key) == d.GetDefault():
                    continue
                target.set(_kebab(d.Name), d.Serialize(b.Values.get(ri.Key)))
            elif d.Kind == "value":
                child = ctx.Value(ri.Key)
                if child is not None:
                    target.append(self.Wrap(_kebab(d.Name), child))
                elif d.Shadow is not None:
                    v = b.Values.get(ri.Key)
                    if not (self.OmitDefaults and v == d.Shadow.GetDefault()):
                        target.set(_kebab(d.Name), d.Shadow.Serialize(v))
            elif d.Kind == "statement":
                stmts = ctx.Statements(ri.Key)
                if stmts:
                    target.append(self.Wrap(_kebab(d.Name), stmts))
        return el

    def Finish(self, fragments, issues) -> str:
        root = ET.Element(self.RootTag)
        for f in fragments:
            root.append(f)
        ET.indent(root, space=self.Indent)
        return ET.tostring(root, encoding="unicode").replace(" />", "/>")


# -------------------------------------------------------------------------------------------- JSON
class JsonTarget(CodeTarget):
    Name = "json"
    Extension = ".json"

    def DefaultEmit(self, ctx):
        b = ctx.Block
        out: dict = {"type": b.Definition.TypeId}
        for ri in b.Inputs():
            d = ri.Def
            if d.Kind == "field":
                out.setdefault("fields", {})[ri.Key] = d.Serialize(b.Values.get(ri.Key))
            elif d.Kind == "value":
                child = ctx.Value(ri.Key)
                if child is not None:
                    out.setdefault("inputs", {})[ri.Key] = child
                elif d.Shadow is not None:
                    out.setdefault("fields", {})[ri.Key] = d.Shadow.Serialize(b.Values.get(ri.Key))
            elif d.Kind == "statement":
                out.setdefault("statements", {})[ri.Key] = ctx.Statements(ri.Key)
        return out

    def Finish(self, fragments, issues) -> str:
        return json.dumps({"blocks": fragments}, indent=2)


# -------------------------------------------------------------------------------------------- pseudo code
class PseudoCodeTarget(CodeTarget):
    """Line-based text target (fragments are lists of lines) -- a template for Lua/Python style output."""
    Name = "pseudo"
    Extension = ".txt"

    def DefaultEmit(self, ctx):
        b = ctx.Block
        args, bodies = [], []
        for ri in b.Inputs():
            d = ri.Def
            if d.Kind == "field":
                args.append(f"{d.Name}={d.Serialize(b.Values.get(ri.Key))!r}")
            elif d.Kind == "value":
                c = ctx.Value(ri.Key)
                args.append(f"{d.Name}={c if c else (d.Shadow.Serialize(b.Values.get(ri.Key)) if d.Shadow else 'nil')}")
            elif d.Kind == "statement":
                bodies.append((d.Name, ctx.Statements(ri.Key)))
        head = f"{b.Definition.XmlName.replace('-', '_')}({', '.join(args)})"
        if b.Definition.IsValue:
            return head
        lines = [head]
        for name, stmts in bodies:
            lines.append(f"  {name}:")
            for s in stmts:
                lines += ["    " + l for l in s]
        return [lines]

    def Finish(self, fragments, issues) -> str:
        out = []
        for f in fragments:
            out += f
        return "\n".join(out)


# -------------------------------------------------------------------------------------------- orchestrator
class BlockCodeGenerator:
    def __init__(self, Document):
        self.Document = Document
        self._targets: dict[str, CodeTarget] = {}
        self._gens: dict[tuple, Callable] = {}
        self.LastIssues: list[str] = []
        self.RegisterTarget(XmlBehaviorTarget())
        self.RegisterTarget(JsonTarget())
        self.RegisterTarget(PseudoCodeTarget())

    def RegisterTarget(self, target: CodeTarget):
        self._targets[target.Name] = target

    def Target(self, name: str) -> CodeTarget:
        return self._targets[name]

    def TargetNames(self) -> list:
        return list(self._targets)

    def RegisterGenerator(self, target: str, type_id: str, fn: Callable):
        self._gens[(target, type_id)] = fn

    def Generate(self, Target: str = "xml") -> str:
        target = self._targets[Target]
        issues: list = []
        frags: list = []
        for root in self.Document.SortedRoots():
            d = root.Definition
            if d.IsUnknown:
                issues.append(f"Unknown block type '{d.TypeId}' skipped")
                continue
            if d.IsValue:
                continue
            frags += self._Chain(target, root, issues)
        self.LastIssues = issues
        return target.Finish(frags, issues)

    # ---- internals
    def _Chain(self, target, first, issues) -> list:
        out = []
        b = first
        while b is not None:
            if not b.Disabled:
                out += self._Emit(target, b, issues)
            b = b.Children.get(NEXT)
        return out

    def _Emit(self, target, block, issues) -> list:
        d = block.Definition
        if d.IsUnknown:
            issues.append(f"Unknown block type '{d.TypeId}' skipped")
            return []
        fn = self._gens.get((target.Name, d.TypeId)) or d.Generators.get(target.Name)
        ctx = CodeContext(self, target, block, issues)
        res = fn(ctx) if fn is not None else target.DefaultEmit(ctx)
        if res is None:
            return []
        return list(res) if isinstance(res, (list, tuple)) else [res]
