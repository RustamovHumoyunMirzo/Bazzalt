"""BlockGraphicsItem: Scratch-style shaped block rendering + layout. Pure projection of a BlockModel."""
from __future__ import annotations
import math
from typing import Optional

from PySide6.QtCore import Qt, QRectF, QPointF, QSizeF
from PySide6.QtGui import (QPainterPath, QPainter, QPen, QBrush, QColor, QFont, QFontMetricsF, QPolygonF)
from PySide6.QtWidgets import QGraphicsItem, QGraphicsScene, QStyleOptionGraphicsItem

from .definition import NEXT, BlockShape
from .fields import FieldStyle
from .icons import IconProvider
from .theme import Qc


class Metrics:
    """Geometry constants + fonts derived from a theme."""
    def __init__(self, theme):
        f = QFont(theme.FontFamily)
        f.setPixelSize(theme.FontPixelSize)
        f.setBold(True)
        self.Font = f
        ff = QFont(f)
        ff.setBold(False)
        self.FieldFont = ff
        self.FM = QFontMetricsF(f)
        self.FFM = QFontMetricsF(ff)
        self.PadX = 10
        self.Gap = 6
        self.RowMin = 38
        self.ReporterRow = 30
        self.VPad = 4
        self.FieldH = 24
        self.SocketW = 32
        self.BoolSocketW = 42
        self.MouthMin = 28
        self.Indent = 18
        self.Footer = 26
        self.NotchW = 30
        self.NotchH = 7
        self.NotchOff = 14
        self.Hat = 20
        self.HatW = 84
        self.Corner = theme.CornerRadius
        self.MinW = 56
        self.Icon = 18
        self.Badge = 18


def RoundedPolygon(points) -> QPainterPath:
    """points: list of (x, y, radius). Rounds each corner with a quadratic curve."""
    n = len(points)
    path = QPainterPath()
    cuts = []
    for i, (x, y, r) in enumerate(points):
        if r <= 0:
            cuts.append(((x, y), (x, y)))
            continue
        px, py, _ = points[i - 1]
        nx, ny, _ = points[(i + 1) % n]
        d1 = math.hypot(px - x, py - y) or 1.0
        d2 = math.hypot(nx - x, ny - y) or 1.0
        rr = min(r, d1 / 2, d2 / 2)
        cuts.append(((x + (px - x) / d1 * rr, y + (py - y) / d1 * rr), (x + (nx - x) / d2 * rr, y + (ny - y) / d2 * rr)))
    path.moveTo(*cuts[0][1])
    for i in range(1, n):
        a, b = cuts[i]
        path.lineTo(*a)
        if points[i][2] > 0:
            path.quadTo(points[i][0], points[i][1], *b)
    a, b = cuts[0]
    path.lineTo(*a)
    if points[0][2] > 0:
        path.quadTo(points[0][0], points[0][1], *b)
    path.closeSubpath()
    return path


def HexPath(r: QRectF) -> QPainterPath:
    h = r.height()
    x, y, w = r.x(), r.y(), r.width()
    pts = [(x + h / 2, y, 2), (x + w - h / 2, y, 2), (x + w, y + h / 2, 2),
           (x + w - h / 2, y + h, 2), (x + h / 2, y + h, 2), (x, y + h / 2, 2)]
    return RoundedPolygon(pts)


def PillPath(r: QRectF) -> QPainterPath:
    p = QPainterPath()
    p.addRoundedRect(r, r.height() / 2, r.height() / 2)
    return p


class _Cell:
    __slots__ = ("Kind", "Rect", "Text", "Ri", "Child", "W", "H")

    def __init__(self, Kind, Text="", Ri=None, Child=None):
        self.Kind, self.Text, self.Ri, self.Child = Kind, Text, Ri, Child
        self.Rect = QRectF()
        self.W = self.H = 0.0


class _Row:
    __slots__ = ("Kind", "Cells", "Y", "H", "Key", "W")

    def __init__(self, Kind, Key=None):
        self.Kind, self.Key = Kind, Key
        self.Cells = []
        self.Y = self.H = self.W = 0.0


# ================================================================================================
class BlockViewBase:
    """Shared by BlockCanvas and the toolbox flyout: item registry, sync from document, layout."""

    def InitHost(self, Document, Theme, Registry, Workspace=None, Interactive=True):
        self.Document, self.Theme, self.Registry, self.Workspace = Document, Theme, Registry, Workspace
        self.Interactive = Interactive
        self.Items: dict = {}
        self.Icons = IconProvider()
        self.Selection = None
        self.Issues: dict = {}
        self._tw: dict = {}
        self._lw: dict = {}
        self.SetThemeBase(Theme)

    def SetThemeBase(self, theme):
        self.Theme = theme
        self.Metrics = Metrics(theme)
        self.FieldStyle = FieldStyle(theme, self.Metrics.FieldFont, self.TextWidth, self.Metrics.FieldH)
        self._tw.clear()
        self._lw.clear()
        self.Icons._cache.clear()

    def TextWidth(self, text: str) -> float:
        w = self._tw.get(text)
        if w is None:
            w = self.Metrics.FFM.horizontalAdvance(text)
            self._tw[text] = w
        return w

    def LabelWidth(self, text: str) -> float:
        w = self._lw.get(text)
        if w is None:
            w = self.Metrics.FM.horizontalAdvance(text)
            self._lw[text] = w
        return w

    def IsSelected(self, block) -> bool:
        return self.Selection is not None and self.Selection.IsSelected(block)

    def ItemFor(self, block):
        return self.Items.get(block.Id) if block is not None else None

    def StackHeight(self, block) -> float:
        h = 0.0
        b = block
        while b is not None:
            it = self.Items.get(b.Id)
            if it is not None:
                h += it._size.height()
            b = b.Children.get(NEXT)
        return h

    def SetIssues(self, issues):
        old = set(self.Issues)
        self.Issues = {}
        for i in issues:
            self.Issues.setdefault(i.BlockId, []).append(i)
        for bid in old | set(self.Issues):
            it = self.Items.get(bid)
            if it is not None:
                it.UpdateIssues()

    def SyncItems(self, changed, removed):
        doc, scene = self.Document, self.Scene
        live = [bid for bid in changed if bid in doc.Blocks]
        for bid in [b for b in removed if b in doc.Blocks]:   # id deleted and re-created in the same flush
            old = self.Items.pop(bid, None)
            if old is not None:
                for ch in old.childItems():
                    ch.setParentItem(None)
                if old.scene() is not None:
                    old.scene().removeItem(old)
        removed = [b for b in removed if b not in doc.Blocks]
        for bid in live:
            if bid not in self.Items:
                it = BlockGraphicsItem(doc.Blocks[bid], self)
                self.Items[bid] = it
                scene.addItem(it)
        # detach wrongly parented items first (avoids transient cycles), then attach
        for bid in live:
            b = doc.Blocks[bid]
            it = self.Items[bid]
            want = self.Items.get(b.Parent.Id) if b.Parent is not None else None
            if it.parentItem() is not want:
                it.setParentItem(None)
        for bid in live:
            b = doc.Blocks[bid]
            it = self.Items[bid]
            want = self.Items.get(b.Parent.Id) if b.Parent is not None else None
            if it.parentItem() is not want:
                it.setParentItem(want)
            if b.Parent is None:
                it.setPos(b.X, b.Y)
        for bid in removed:
            it = self.Items.pop(bid, None)
            if it is not None:
                for ch in it.childItems():      # C++ would delete children with the parent
                    ch.setParentItem(None)
                if it.scene() is not None:
                    it.scene().removeItem(it)
        for bid in live:
            self.Items[bid].Refresh()
        memo: dict = {}
        roots = []
        for bid in live:
            it = self.Items[bid]
            chain = []
            cur = it
            while True:
                if id(cur) in memo:
                    root = memo[id(cur)]
                    break
                chain.append(cur)
                par = cur.parentItem()
                if par is None:
                    root = cur
                    break
                cur = par
            for c in chain:
                memo[id(c)] = root
            if root not in roots:
                roots.append(root)
        # parents of removed blocks may have lost children: caller passes them in `changed` (they were touched)
        for r in roots:
            r.LayoutTree(False)
        self.scene().update()

    def RootItems(self):
        return [it for it in self.Items.values() if it.parentItem() is None]


# ================================================================================================
class BlockGraphicsItem(QGraphicsItem):
    def __init__(self, block, host):
        super().__init__()
        self.Block = block
        self.Host = host
        self._rows: list = []
        self._size = QSizeF(80, 36)
        self._path: Optional[QPainterPath] = None
        self._bounds = QRectF(-4, -4, 88, 44)
        self._hot: list = []
        self._sockets: dict = {}
        self._mouths: dict = {}
        self._disabled = False
        self._color = Qc("#888888")
        self._bubble = None
        self._hat = False
        self._layout_sig = None
        self.setAcceptedMouseButtons(Qt.NoButton)
        self.setFlag(QGraphicsItem.ItemIsSelectable, False)

    # ----------------------------------------------------------------- geometry API
    @property
    def Width(self):
        return self._size.width()

    @property
    def Height(self):
        return self._size.height()

    def boundingRect(self) -> QRectF:
        return self._bounds

    def shape(self) -> QPainterPath:
        if self._path is None:
            self._path = self._BuildPath()
        return self._path

    def RootItem(self):
        it = self
        while it.parentItem() is not None:
            it = it.parentItem()
        return it

    def SocketRect(self, key) -> Optional[QRectF]:
        return self._sockets.get(key)

    def MouthRect(self, key) -> Optional[QRectF]:
        return self._mouths.get(key)

    def HotAt(self, pos: QPointF):
        for kind, rect, payload in reversed(self._hot):
            if rect.contains(pos):
                return kind, rect, payload
        return None

    # ----------------------------------------------------------------- refresh / layout
    def Refresh(self):
        d = self.Block.Definition
        self._color = Qc(d.ColorFor(self.Host.Theme))
        self._ApplyChildVisibility()
        self._SyncBubble()
        tip = d.Tooltip or ""
        self.setToolTip(tip)
        self.UpdateIssues()
        self.update()

    def UpdateIssues(self):
        issues = self.Host.Issues.get(self.Block.Id)
        if issues:
            self.setToolTip("\n".join(f"{i.Severity}: {i.Message}" for i in issues))
        else:
            self.setToolTip(self.Block.Definition.Tooltip or "")
        self.update()

    def _ApplyChildVisibility(self):
        b = self.Block
        for slot, c in b.Children.items():
            if slot == NEXT:
                continue
            it = self.Host.Items.get(c.Id)
            if it is not None:
                it.setVisible(not b.Collapsed)

    def _NextItem(self):
        n = self.Block.Children.get(NEXT)
        return self.Host.Items.get(n.Id) if n is not None else None

    def LayoutTree(self, inherited_disabled: bool = False):
        chain = []
        it = self
        while it is not None:
            chain.append(it)
            it = it._NextItem()
        host = self.Host
        for it in chain:
            it._disabled = inherited_disabled or it.Block.Disabled
            b = it.Block
            if not b.Collapsed:
                for slot, c in b.Children.items():
                    if slot == NEXT:
                        continue
                    ci = host.Items.get(c.Id)
                    if ci is not None:
                        ci.LayoutTree(it._disabled)
            it.LayoutSelf()
        for prev, nxt in zip(chain, chain[1:]):
            p = QPointF(0, prev._size.height())
            if nxt.pos() != p:
                nxt.setPos(p)

    def LayoutSelf(self):
        host = self.Host
        M = host.Metrics
        b = self.Block
        d = b.Definition
        reporter = d.Shape in (BlockShape.Value, BlockShape.Boolean)
        rowmin = M.ReporterRow if reporter else M.RowMin
        rows: list = []
        cur = _Row("bar")
        if b.Collapsed:
            txt = d.Instance.Summary(b)
            cur.Cells.append(_Cell("label", self._Elide(txt, 280)))
            rows.append(cur)
        else:
            pm = host.Icons.Pixmap(d.Icon, M.Icon, "#FFFFFF") if d.Icon else None
            if pm is not None:
                cur.Cells.append(_Cell("icon"))
            if d.IsUnknown:
                cur.Cells.append(_Cell("label", d.DisplayName))
            for ri in b.Inputs():
                inp = ri.Def
                k = inp.Kind
                if k == "label":
                    cur.Cells.append(_Cell("label", inp.Text))
                    continue
                if inp.Label:
                    cur.Cells.append(_Cell("label", inp.Label))
                if k == "field":
                    cur.Cells.append(_Cell("field", Ri=ri))
                elif k == "value":
                    c = b.Children.get(ri.Key)
                    cur.Cells.append(_Cell("socket", Ri=ri, Child=host.Items.get(c.Id) if c is not None else None))
                elif k == "statement":
                    rows.append(cur)
                    rows.append(_Row("mouth", ri.Key))
                    cur = _Row("bar")
            if cur.Cells or not rows or rows[-1].Kind == "mouth":
                cur.Key = "footer" if (not cur.Cells and rows and rows[-1].Kind == "mouth") else None
                rows.append(cur)
        st = host.FieldStyle
        has_mut = d.HasSections and not b.Collapsed
        first = True
        maxw = M.MinW
        for r in rows:
            if r.Kind == "mouth":
                ch = b.Children.get(r.Key)
                r.H = max(M.MouthMin, host.StackHeight(ch) if ch is not None else 0)
                continue
            w = M.PadX * 2
            mh = 0.0
            for c in r.Cells:
                k = c.Kind
                if k == "label":
                    c.W, c.H = host.LabelWidth(c.Text), M.FM.height()
                elif k == "icon":
                    c.W = c.H = M.Icon
                elif k == "field":
                    c.W, c.H = c.Ri.Def.Measure(b.Values.get(c.Ri.Key), st)
                else:  # socket
                    inp = c.Ri.Def
                    if c.Child is not None:
                        c.W, c.H = c.Child._size.width(), c.Child._size.height()
                    elif inp.Shadow is not None:
                        c.W, c.H = inp.Shadow.Measure(b.Values.get(c.Ri.Key), st)
                    else:
                        c.W = M.BoolSocketW if inp.Boolean else M.SocketW
                        c.H = M.FieldH
                w += c.W
                mh = max(mh, c.H)
            if r.Cells:
                w += M.Gap * (len(r.Cells) - 1)
            if first and has_mut:
                w += M.Badge + M.Gap
            r.W = w
            r.H = M.Footer if (r.Key == "footer") else max(rowmin, mh + 2 * M.VPad)
            first = False
            maxw = max(maxw, w)
        W = maxw
        y = 0.0
        for r in rows:
            r.Y = y
            y += r.H
        H = y
        # position cells + children
        self._hot = []
        self._sockets = {}
        self._mouths = {}
        first = True
        for r in rows:
            if r.Kind == "mouth":
                ch = b.Children.get(r.Key)
                self._mouths[r.Key] = QRectF(M.Indent, r.Y, max(W - M.Indent, 20), r.H)
                if ch is not None:
                    ci = host.Items.get(ch.Id)
                    if ci is not None:
                        pt = QPointF(M.Indent, r.Y)
                        if ci.pos() != pt:
                            ci.setPos(pt)
                continue
            x = M.PadX
            for c in r.Cells:
                c.Rect = QRectF(x, r.Y + (r.H - c.H) / 2, c.W, c.H)
                x += c.W + M.Gap
                if c.Kind == "field":
                    self._hot.append(("field", c.Rect, c.Ri))
                elif c.Kind == "socket":
                    self._sockets[c.Ri.Key] = c.Rect
                    if c.Child is not None:
                        pt = c.Rect.topLeft()
                        if c.Child.pos() != pt:
                            c.Child.setPos(pt)
                    elif c.Ri.Def.Shadow is not None:
                        self._hot.append(("field", c.Rect, c.Ri))
            if first and has_mut:
                self._hot.append(("mutator", QRectF(W - M.PadX - M.Badge + 2, r.Y + (r.H - M.Badge) / 2, M.Badge, M.Badge), None))
            first = False
        if b.Comment:
            self._hot.append(("comment", QRectF(W - 10, -10, 20, 20), None))
        self._hot.append(("warning", QRectF(-9, -9, 18, 18), None))
        nxt = self._NextItem()
        if nxt is not None:
            pt = QPointF(0, H)
            if nxt.pos() != pt:
                nxt.setPos(pt)
        hat = (not d.HasPrevious) and d.Shape == BlockShape.Entry
        newsize = QSizeF(W, H)
        if newsize != self._size or hat != self._hat or self._rows_sig(rows) != self._layout_sig:
            self.prepareGeometryChange()
        self._size = newsize
        self._hat = hat
        self._layout_sig = self._rows_sig(rows)
        self._rows = rows
        self._path = None
        top = (M.Hat if hat else 0) + 6
        bot = (M.NotchH if d.HasNext and not reporter else 0) + 6
        self._bounds = QRectF(-6, -top, W + 12, H + top + bot)
        self._PositionBubble()
        self.update()

    @staticmethod
    def _rows_sig(rows):
        return tuple((r.Kind, round(r.H, 1)) for r in rows)

    def _Elide(self, text, maxw):
        return self.Host.Metrics.FM.elidedText(text, Qt.ElideRight, maxw)

    # ----------------------------------------------------------------- shape
    def _BuildPath(self) -> QPainterPath:
        host = self.Host
        M = host.Metrics
        d = self.Block.Definition
        custom = d.Instance.BuildShape(self)
        if custom is not None:
            return custom
        W, H = self._size.width(), self._size.height()
        if d.Shape == BlockShape.Value:
            return PillPath(QRectF(0, 0, W, H))
        if d.Shape == BlockShape.Boolean:
            return HexPath(QRectF(0, 0, W, H))
        r = M.Corner
        NO, NW, NH, MI = M.NotchOff, M.NotchW, M.NotchH, M.Indent
        pts = []
        if d.HasPrevious:
            pts += [(0, 0, r), (NO, 0, 0), (NO + 4, NH, 0), (NO + NW - 4, NH, 0), (NO + NW, 0, 0)]
        elif self._hat:
            hw = min(M.HatW, W * 0.8)
            pts.append((0, 0, r))
            n = 12
            for i in range(1, n + 1):
                t = i / n
                x = 2 * (1 - t) * t * (hw / 2) + t * t * hw
                y = 2 * (1 - t) * t * (-2 * M.Hat)
                pts.append((x, y, 0))
        else:
            pts.append((0, 0, r))
        pts.append((W, 0, r))
        for row in self._rows:
            if row.Kind != "mouth":
                continue
            y = row.Y
            pts.append((W, y, r))
            pts += [(MI + NO + NW, y, 0), (MI + NO + NW - 4, y + NH, 0), (MI + NO + 4, y + NH, 0), (MI + NO, y, 0)]
            pts.append((MI, y, 3))
            pts.append((MI, y + row.H, 3))
            pts.append((W, y + row.H, r))
        pts.append((W, H, r))
        if d.HasNext:
            pts += [(NO + NW, H, 0), (NO + NW - 4, H + NH, 0), (NO + 4, H + NH, 0), (NO, H, 0)]
        pts.append((0, H, r))
        return RoundedPolygon(pts)

    # ----------------------------------------------------------------- comment bubble
    def _SyncBubble(self):
        b = self.Block
        want = bool(b.Comment) and b.CommentOpen and self.Host.Interactive and not b.Collapsed
        if want and self._bubble is None:
            self._bubble = CommentBubbleItem(self)
        elif not want and self._bubble is not None:
            if self._bubble.scene() is not None:
                self._bubble.scene().removeItem(self._bubble)
            self._bubble.setParentItem(None)
            self._bubble = None
        if self._bubble is not None:
            self._bubble.SetText(b.Comment)
            self._PositionBubble()

    def _PositionBubble(self):
        if self._bubble is not None:
            self._bubble.setPos(self._size.width() + 18, -10)

    # ----------------------------------------------------------------- painting
    def paint(self, p: QPainter, option: QStyleOptionGraphicsItem, widget=None):
        host = self.Host
        th = host.Theme
        M = host.Metrics
        lod = option.levelOfDetailFromTransform(p.worldTransform())
        path = self.shape()
        d = self.Block.Definition
        color = Qc(th.DisabledColor) if self._disabled else self._color
        p.setRenderHint(QPainter.Antialiasing, True)
        if lod < 0.4:
            p.setPen(Qt.NoPen)
            p.setBrush(color)
            p.drawPath(path)
            if host.IsSelected(self.Block):
                p.setBrush(Qt.NoBrush)
                p.setPen(QPen(Qc(th.SelectionColor), 3))
                p.drawPath(path)
            return
        p.setPen(QPen(color.darker(th.OutlineDarken), 1.3))
        p.setBrush(color)
        p.drawPath(path)
        if self._disabled:
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(QColor(255, 255, 255, 110), Qt.BDiagPattern))
            p.drawPath(path)
        if host.IsSelected(self.Block):
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(Qc(th.SelectionColor), 3.2))
            p.drawPath(path)
        if d.IsUnknown:
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(QColor(90, 90, 90), 1.4, Qt.DashLine))
            p.drawPath(path)
        textc = Qc(th.DisabledText) if self._disabled else Qc(th.TextColor)
        st = host.FieldStyle
        st.Disabled = self._disabled
        b = self.Block
        sock_dark = color.darker(128)
        for r in self._rows:
            if r.Kind == "mouth":
                continue
            for c in r.Cells:
                k = c.Kind
                if k == "label":
                    p.setFont(M.Font)
                    p.setPen(textc)
                    p.drawText(c.Rect, int(Qt.AlignVCenter | Qt.AlignLeft), c.Text)
                elif k == "icon":
                    pm = host.Icons.Pixmap(d.Icon, M.Icon, "#FFFFFF")
                    if pm is not None:
                        p.drawPixmap(c.Rect.topLeft(), pm)
                elif k == "field":
                    c.Ri.Def.Paint(p, c.Rect, b.Values.get(c.Ri.Key), st)
                else:
                    inp = c.Ri.Def
                    if c.Child is not None:
                        p.setPen(Qt.NoPen)
                        p.setBrush(sock_dark)
                        p.drawPath(HexPath(c.Rect) if inp.Boolean or c.Child.Block.Definition.Shape == BlockShape.Boolean else PillPath(c.Rect))
                    elif inp.Shadow is not None:
                        inp.Shadow.Paint(p, c.Rect, b.Values.get(c.Ri.Key), st)
                    elif inp.Boolean:
                        p.setPen(Qt.NoPen)
                        p.setBrush(sock_dark)
                        p.drawPath(HexPath(c.Rect))
                    else:
                        p.setPen(Qt.NoPen)
                        p.setBrush(Qc(th.FieldFill) if not self._disabled else Qc(th.DisabledText))
                        p.drawPath(PillPath(c.Rect))
        for kind, rect, payload in self._hot:
            if kind == "mutator":
                pm = host.Icons.Pixmap("gear", 14, "#FFFFFF")
                p.setPen(Qt.NoPen)
                p.setBrush(QColor(255, 255, 255, 55))
                p.drawEllipse(rect)
                if pm is not None:
                    p.drawPixmap(QPointF(rect.x() + 2, rect.y() + 2), pm)
            elif kind == "comment":
                p.setPen(QPen(Qc(th.CommentText).darker(110), 1))
                p.setBrush(Qc(th.CommentFill))
                p.drawEllipse(rect.adjusted(3, 3, -3, -3))
                p.setPen(Qc(th.CommentText))
                p.setFont(M.Font)
                p.drawText(rect, int(Qt.AlignCenter), "i")
            elif kind == "warning" and host.Issues.get(b.Id):
                sev = "Error" if any(i.Severity == "Error" for i in host.Issues[b.Id]) else "Warning"
                p.setPen(QPen(QColor("white"), 1.4))
                p.setBrush(Qc(th.ErrorColor if sev == "Error" else th.WarningColor))
                p.drawEllipse(rect)
                p.setFont(M.Font)
                p.drawText(rect, int(Qt.AlignCenter), "!")
        d.Instance.PaintDecoration(p, self, th)


class CommentBubbleItem(QGraphicsItem):
    """Sticky-note bubble attached to a block."""
    WIDTH = 190

    def __init__(self, owner: BlockGraphicsItem):
        super().__init__(owner)
        self.Owner = owner
        self._text = ""
        self._rect = QRectF(0, 0, self.WIDTH, 40)
        self.setZValue(20)

    def SetText(self, text: str):
        self._text = text
        fm = self.Owner.Host.Metrics.FFM
        r = fm.boundingRect(QRectF(0, 0, self.WIDTH - 16, 4000), int(Qt.TextWordWrap), text)
        self.prepareGeometryChange()
        self._rect = QRectF(0, 0, self.WIDTH, max(34, r.height() + 16))
        self.update()

    def boundingRect(self):
        return self._rect.adjusted(-22, -2, 2, 2)

    def paint(self, p, option, widget=None):
        th = self.Owner.Host.Theme
        p.setRenderHint(QPainter.Antialiasing, True)
        p.setPen(QPen(Qc(th.CommentText).darker(120), 1))
        p.drawLine(QPointF(-18, 10), QPointF(0, 10))
        p.setBrush(Qc(th.CommentFill))
        p.drawRoundedRect(self._rect, 6, 6)
        p.setPen(Qc(th.CommentText))
        p.setFont(self.Owner.Host.Metrics.FieldFont)
        p.drawText(self._rect.adjusted(8, 8, -8, -8), int(Qt.TextWordWrap | Qt.AlignLeft | Qt.AlignTop), self._text)
