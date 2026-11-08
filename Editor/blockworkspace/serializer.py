"""BlockSerializer: workspace XML (layout + structure) -- distinct from generated runtime code."""
from __future__ import annotations
import xml.etree.ElementTree as ET
from typing import Callable, Iterable, Optional

try:  # hardened parser when available
    from defusedxml import ElementTree as _SafeET
    _fromstring = _SafeET.fromstring
except Exception:  # pragma: no cover
    _fromstring = ET.fromstring

from .definition import BlockDefinition, NEXT, BlockShape
from .model import BlockModel, NewBlockId

FORMAT_VERSION = "1"
_KNOWN_ATTRS = {"type", "id", "shape", "output", "disabled", "collapsed"}


class ImportReport:
    def __init__(self):
        self.Warnings: list[str] = []
        self.UnknownTypes: set[str] = set()
        self.Roots: list[BlockModel] = []
        self.Blocks: list[BlockModel] = []
        self.View: Optional[dict] = None


class BlockSerializer:
    def __init__(self, Document, Registry):
        self.Document = Document
        self.Registry = Registry

    # =================================================================== export
    def ExportWorkspaceXml(self, View: Optional[dict] = None) -> str:
        return self._Export(self.Document.SortedRoots(), None, View)

    def ExportBlocksXml(self, roots: Iterable[BlockModel], next_pred: Optional[Callable] = None, positions: Optional[dict] = None) -> str:
        """Serialize ``roots`` (each with nested children). ``next_pred(block)`` decides whether the *Next* block of
        a root chain is included (None -> include whole chain)."""
        return self._Export(list(roots), next_pred, None, positions)

    def _Export(self, roots, next_pred, view, positions=None) -> str:
        w = ET.Element("workspace", version=FORMAT_VERSION)
        if view:
            ET.SubElement(w, "view", {k: repr(v) if isinstance(v, float) else str(v) for k, v in view.items()})
        for r in roots:
            px, py = (positions or {}).get(r.Id, (r.X, r.Y))
            st = ET.SubElement(w, "stack", x=repr(round(px, 3)), y=repr(round(py, 3)))
            self._WriteChain(st, r, next_pred)
        ET.indent(w, space="  ")
        return ET.tostring(w, encoding="unicode")

    def _WriteChain(self, parent, first, pred=None):
        cur = first
        while cur is not None:
            self._WriteBlock(parent, cur)
            nxt = cur.Next
            if nxt is None or (pred is not None and not pred(nxt)):
                break
            cur = nxt

    def _WriteBlock(self, parent, b: BlockModel):
        d = b.Definition
        e = ET.SubElement(parent, "block", type=d.TypeId, id=b.Id)
        e.set("shape", d.Shape)
        if d.OutputType and d.IsValue:
            e.set("output", d.OutputType)
        if b.Disabled:
            e.set("disabled", "true")
        if b.Collapsed:
            e.set("collapsed", "true")
        if b.IsUnknown and b.Raw:
            for k, v in (b.Raw.get("Attrs") or {}).items():
                e.set(k, v)
        if b.Comment:
            c = ET.SubElement(e, "comment", {"open": "true" if b.CommentOpen else "false"})
            c.text = b.Comment
        if b.IsUnknown:
            m = b.Raw.get("Mutation") if b.Raw else None
            if m:
                e.append(ET.fromstring(m))
        elif d.SectionDefs:
            m = ET.SubElement(e, "mutation", {"next-uid": str(b.NextUid)})
            for n, u in b.Sections:
                ET.SubElement(m, "section", name=n, uid=str(u))
        for key, v in b.Values.items():
            ri = b.InputByKey(key)
            f = ET.SubElement(e, "field", name=key)
            if b.IsUnknown or ri is None:
                f.text = "" if v is None else str(v)
            elif ri.Kind == "field":
                f.text = ri.Def.Serialize(v)
            elif ri.Kind == "value" and ri.Def.Shadow is not None:
                f.text = ri.Def.Shadow.Serialize(v)
            else:
                f.text = "" if v is None else str(v)
        meta = d.Instance.SerializeMetadata(b) if not b.IsUnknown else dict(b.Metadata)
        for k, v in (meta or {}).items():
            me = ET.SubElement(e, "meta", key=str(k))
            me.text = str(v)
        for ri in b.Inputs():
            if ri.Kind not in ("value", "statement"):
                continue
            child = b.Children.get(ri.Key)
            if child is None:
                continue
            slot = ET.SubElement(e, ri.Kind, name=ri.Key)
            if ri.Kind == "value":
                self._WriteBlock(slot, child)
            else:
                self._WriteChain(slot, child, None)

    # =================================================================== import
    def ImportWorkspaceXml(self, xml: str, Clear: bool = True, Undoable: bool = False, NewIds: bool = False,
                           Offset=(0.0, 0.0)) -> ImportReport:
        root = _fromstring(xml)
        if root.tag != "workspace":
            raise ValueError("Not a workspace XML document (expected <workspace> root)")
        doc = self.Document
        report = ImportReport()
        orphans: list = []

        def work():
            if Clear:
                for b in list(doc.Roots.values()):
                    for x in list(b.Walk()):
                        doc._DeleteRaw(x)
                doc.Blocks.clear()
                doc.Roots.clear()
            for st in root.findall("stack"):
                x = float(st.get("x", 0)) + Offset[0]
                y = float(st.get("y", 0)) + Offset[1]
                head = self._ReadChain(st.findall("block"), report, NewIds, orphans)
                if head is not None:
                    head.X, head.Y = x, y
                    report.Roots.append(head)
            for i, o in enumerate(orphans):
                o.X, o.Y = Offset[0] + 40 + 30 * i, Offset[1] + 40 + 30 * i
                report.Roots.append(o)
            v = root.find("view")
            if v is not None:
                report.View = dict(v.attrib)
            for b in report.Blocks:
                if b.Id in doc.Blocks:
                    doc.BlockAdded.emit(b)

        if Undoable:
            with doc.Transaction("Import Workspace"):
                work()
        else:
            with doc.Commands.Suspended():
                with doc.Transaction("Import Workspace"):
                    work()
            doc.Commands.Clear()
        return report

    def _ReadChain(self, elements, report, newids, orphans):
        head, prev = None, None
        doc = self.Document
        for el in elements:
            b = self._ReadBlock(el, report, newids, orphans)
            if head is None:
                head = b
            else:
                if doc.Connections.CanConnect(prev, NEXT, b).Ok:
                    doc._Link(prev, NEXT, b)
                else:
                    report.Warnings.append(f"Block {b.Id} could not be attached below {prev.Id}")
                    orphans.append(b)
            prev = b
        return head

    def _ReadBlock(self, el, report, newids, orphans) -> BlockModel:
        doc = self.Document
        tid = el.get("type", "")
        d = self.Registry.Get(tid)
        raw = None
        if d is None:
            report.UnknownTypes.add(tid)
            fields, slots = [], []
            for ch in el:
                if ch.tag == "field":
                    fields.append(ch.get("name"))
                elif ch.tag in ("value", "statement"):
                    slots.append((ch.get("name"), ch.tag))
            mut = el.find("mutation")
            raw = {"Shape": el.get("shape"), "Output": el.get("output"),
                   "Attrs": {k: v for k, v in el.attrib.items() if k not in _KNOWN_ATTRS},
                   "Fields": fields, "Slots": slots,
                   "Mutation": ET.tostring(mut, encoding="unicode") if mut is not None else None}
            d = BlockDefinition.Unknown(tid, raw)
        bid = el.get("id")
        if newids or not bid or bid in doc.Blocks:
            bid = NewBlockId()
        b = BlockModel(bid, d)
        b.Raw = raw
        doc.Commands.TouchNew(b)
        doc.Blocks[bid] = b
        doc.Roots[bid] = b
        doc._dirty.add(bid)
        report.Blocks.append(b)
        b.Disabled = el.get("disabled") == "true"
        b.Collapsed = el.get("collapsed") == "true"
        if not b.IsUnknown:
            m = el.find("mutation")
            if m is not None:
                secs = []
                for s in m.findall("section"):
                    n, u = s.get("name"), int(s.get("uid", "0"))
                    if n in d.SectionDefs:
                        secs.append((n, u))
                    else:
                        report.Warnings.append(f"{tid}: unknown section '{n}' ignored")
                b.Sections = secs
                b.NextUid = int(m.get("next-uid", str(max([u for _, u in secs], default=0) + 1)))
            else:
                for sd in d.SectionDefs.values():
                    for _ in range(sd.Default):
                        b.Sections.append((sd.Name, b.NextUid))
                        b.NextUid += 1
            b.InvalidateInputs()
            doc._InitDefaults(b)
        meta = {}
        for ch in el:
            if ch.tag == "field":
                key, text = ch.get("name"), ch.text or ""
                ri = b.InputByKey(key)
                if b.IsUnknown or ri is None:
                    b.Values[key] = text
                elif ri.Kind == "field":
                    b.Values[key] = ri.Def.Deserialize(text)
                elif ri.Kind == "value" and ri.Def.Shadow is not None:
                    b.Values[key] = ri.Def.Shadow.Deserialize(text)
                else:
                    b.Values[key] = text
            elif ch.tag == "comment":
                b.Comment = ch.text or ""
                b.CommentOpen = ch.get("open") == "true"
            elif ch.tag == "meta":
                meta[ch.get("key")] = ch.text or ""
        if meta:
            if b.IsUnknown:
                b.Metadata.update(meta)
            else:
                d.Instance.DeserializeMetadata(b, meta)
        for ch in el:
            if ch.tag not in ("value", "statement"):
                continue
            slot = ch.get("name")
            kids = ch.findall("block")
            if not kids:
                continue
            head = self._ReadChain(kids if ch.tag == "statement" else kids[:1], report, newids, orphans)
            ri = b.InputByKey(slot)
            if ri is not None and ri.Kind == ch.tag and doc.Connections.CanConnect(b, slot, head).Ok:
                doc._Link(b, slot, head)
            else:
                report.Warnings.append(f"{tid}: could not attach child to slot '{slot}'")
                orphans.append(head)
        return b
