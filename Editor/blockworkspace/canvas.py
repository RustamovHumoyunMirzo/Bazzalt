"""BlockCanvas: QGraphicsView hosting block items, with drag/snap, selection rectangle, zoom/pan and menus."""
from __future__ import annotations
import math
from typing import Optional

from PySide6.QtCore import Qt, QPointF, QRectF, QLineF, QPoint, Signal, QTimer
from PySide6.QtGui import QPainter, QPen, QBrush, QColor, QTransform, QPainterPath
from PySide6.QtWidgets import (QGraphicsView, QGraphicsScene, QGraphicsItem, QApplication, QToolButton, QMenu,
                               QToolTip)

from .definition import NEXT
from .fields import FieldEditContext
from .graphics import BlockViewBase, BlockGraphicsItem, CommentBubbleItem
from .theme import Qc


class ConnectionPreviewItem(QGraphicsItem):
    """Scene-level overlay: compatible connection points + ghost of the block at the snap position."""
    def __init__(self):
        super().__init__()
        self.setZValue(5000)
        self.setAcceptedMouseButtons(Qt.NoButton)
        self._rect = QRectF()
        self._points: list = []
        self._best: Optional[QPointF] = None
        self._ghost: Optional[QPainterPath] = None
        self._theme = None

    def SetState(self, theme, points, best, ghost):
        self.prepareGeometryChange()
        self._theme, self._points, self._best, self._ghost = theme, points, best, ghost
        r = QRectF()
        for p in points + ([best] if best else []):
            r = r.united(QRectF(p.x() - 12, p.y() - 12, 24, 24))
        if ghost is not None:
            r = r.united(ghost.boundingRect().adjusted(-8, -8, 8, 8))
        self._rect = r
        self.update()

    def Clear(self):
        self.SetState(self._theme, [], None, None)

    def boundingRect(self):
        return self._rect

    def paint(self, p, option, widget=None):
        if self._theme is None:
            return
        p.setRenderHint(QPainter.Antialiasing, True)
        hl = Qc(self._theme.InsertionHighlight)
        for pt in self._points:
            c = QColor(hl)
            c.setAlpha(150)
            p.setPen(Qt.NoPen)
            p.setBrush(c)
            p.drawEllipse(pt, 4.5, 4.5)
        if self._ghost is not None:
            g = Qc(self._theme.PreviewColor)
            g.setAlpha(90)
            p.setBrush(g)
            p.setPen(QPen(hl, 2.5, Qt.DashLine))
            p.drawPath(self._ghost)
        if self._best is not None:
            p.setPen(QPen(hl, 3))
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(self._best, 9, 9)


class _DragState:
    def __init__(self):
        self.Roots: list = []
        self.Items: list = []
        self.Orig: list = []
        self.Anchor = QPointF()
        self.Primary = None
        self.External = False
        self.Snap = None


class BlockCanvas(QGraphicsView, BlockViewBase):
    ZoomChanged = Signal(float)

    def __init__(self, Workspace):
        QGraphicsView.__init__(self)
        self.Scene = QGraphicsScene(self)
        self.Scene.setSceneRect(-60000, -60000, 120000, 120000)
        self.setScene(self.Scene)
        self.InitHost(Workspace.Document, Workspace.Theme, Workspace.Registry, Workspace, True)
        self.Selection = Workspace.Selection
        self.Connections = Workspace.Connections
        self.GridVisible = True
        self.GridSize = 24
        self.SnapToGrid = False
        self.ReadOnly = False
        self.WheelAction = "Zoom"   # or "Scroll"
        self.MinZoom, self.MaxZoom = 0.1, 3.0
        self.SnapRadius = 34.0
        self._zoom = 1.0
        self._press = None
        self._drag: Optional[_DragState] = None
        self._band = None
        self._pan = None
        self._space = False
        self._view_ready = False
        self._last_sel: set = set()
        self.Preview = ConnectionPreviewItem()
        self.Scene.addItem(self.Preview)
        self.setRenderHints(QPainter.Antialiasing | QPainter.TextAntialiasing | QPainter.SmoothPixmapTransform)
        self.setViewportUpdateMode(QGraphicsView.SmartViewportUpdate)
        self.setTransformationAnchor(QGraphicsView.NoAnchor)
        self.setResizeAnchor(QGraphicsView.NoAnchor)
        self.setDragMode(QGraphicsView.NoDrag)
        self.setFrameShape(QGraphicsView.NoFrame)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setOptimizationFlag(QGraphicsView.DontSavePainterState, False)
        self.Document.Updated.connect(self.SyncItems)
        self.Selection.Changed.connect(self._OnSelectionChanged)
        self.Document.PositionProvider = self._PositionOf
        self._MakeOverlay()

    # ================================================================== misc plumbing
    def _PositionOf(self, block):
        it = self.Items.get(block.Id)
        if it is None:
            return None
        p = it.scenePos()
        return (p.x(), p.y())

    def SetTheme(self, theme):
        self.SetThemeBase(theme)
        for it in self.Items.values():
            it._layout_sig = None
            it.Refresh()
        for r in self.RootItems():
            r.LayoutTree(False)
        self._StyleOverlay()
        self.viewport().update()

    def _OnSelectionChanged(self):
        cur = self.Selection.SelectedIds()
        for bid in cur ^ self._last_sel:
            it = self.Items.get(bid)
            if it is not None:
                it.update()
        self._last_sel = cur

    def BlockAt(self, scene_pos: QPointF):
        for it in self.Scene.items(scene_pos):
            if isinstance(it, BlockGraphicsItem) and it.isVisible() and it.shape().contains(it.mapFromScene(scene_pos)):
                return it
        return None

    # ================================================================== overlay controls
    def _MakeOverlay(self):
        self._btns = []
        for text, tip, fn in (("+", "Zoom in", lambda: self.ZoomBy(1.2)), ("−", "Zoom out", lambda: self.ZoomBy(1 / 1.2)),
                              ("⤢", "Zoom to fit", self.ZoomToFit)):
            b = QToolButton(self.viewport())
            b.setText(text)
            b.setToolTip(tip)
            b.setFixedSize(30, 30)
            b.setCursor(Qt.ArrowCursor)
            b.setFocusPolicy(Qt.NoFocus)
            b.clicked.connect(fn)
            self._btns.append(b)
        self._StyleOverlay()

    def _StyleOverlay(self):
        th = self.Theme
        for b in self._btns:
            b.setStyleSheet(f"QToolButton{{background:{th.ToolboxBackground};color:{th.ToolboxText};border:1px solid {th.ToolboxBorder};"
                            f"border-radius:15px;font-size:16px;}}QToolButton:hover{{background:{th.ToolboxSelected};}}")

    def _LayoutOverlay(self):
        w, h = self.viewport().width(), self.viewport().height()
        y = h - 12 - 30
        for b in reversed(self._btns):
            b.move(w - 12 - 30, y)
            y -= 36

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self._LayoutOverlay()
        if not self._view_ready and self.viewport().width() > 10:
            self.ResetView()

    def showEvent(self, e):
        super().showEvent(e)
        if not self._view_ready:
            self.ResetView()

    # ================================================================== zoom / pan
    @property
    def Zoom(self) -> float:
        return self._zoom

    def SetZoom(self, z: float, anchor: Optional[QPointF] = None):
        z = max(self.MinZoom, min(self.MaxZoom, float(z)))
        if anchor is None:
            anchor = QPointF(self.viewport().rect().center())
        before = self.mapToScene(anchor.toPoint())
        self._zoom = z
        self.setTransform(QTransform.fromScale(z, z))
        after = self.mapToScene(anchor.toPoint())
        c = self.mapToScene(self.viewport().rect().center()) + (before - after)
        self.centerOn(c)
        self.ZoomChanged.emit(z)
        self.viewport().update()

    def ZoomBy(self, f: float, anchor: Optional[QPointF] = None):
        self.SetZoom(self._zoom * f, anchor)

    def ResetZoom(self):
        self.SetZoom(1.0)

    def ResetView(self):
        if self.viewport().width() < 10:
            return
        self._view_ready = True
        self.centerOn(self.viewport().width() / 2 / self._zoom - 40, self.viewport().height() / 2 / self._zoom - 40)

    def ContentRect(self) -> QRectF:
        r = QRectF()
        for it in self.RootItems():
            if it is self.Preview:
                continue
            r = r.united(it.mapRectToScene(it.childrenBoundingRect().united(it.boundingRect())))
        return r

    def ZoomToFit(self, padding: float = 48.0):
        r = self.ContentRect()
        if r.isEmpty():
            self.SetZoom(1.0)
            self.ResetView()
            return
        vw, vh = max(self.viewport().width(), 1), max(self.viewport().height(), 1)
        z = min(vw / (r.width() + 2 * padding), vh / (r.height() + 2 * padding))
        z = max(self.MinZoom, min(1.25, z))
        self._zoom = z
        self.setTransform(QTransform.fromScale(z, z))
        self.centerOn(r.center())
        self.ZoomChanged.emit(z)
        self.viewport().update()

    def CenterOnBlock(self, block):
        it = self.Items.get(block.Id)
        if it is not None:
            self.centerOn(it.scenePos() + QPointF(it.Width / 2, it.Height / 2))

    def ViewCenterScene(self) -> QPointF:
        return self.mapToScene(self.viewport().rect().center())

    def wheelEvent(self, e):
        dy = e.angleDelta().y()
        if e.modifiers() & Qt.ControlModifier or self.WheelAction == "Zoom":
            if dy:
                self.ZoomBy(1.0015 ** dy, e.position())
            e.accept()
            return
        h, v = self.horizontalScrollBar(), self.verticalScrollBar()
        pd = e.pixelDelta()
        if not pd.isNull():
            h.setValue(h.value() - pd.x())
            v.setValue(v.value() - pd.y())
        elif e.modifiers() & Qt.ShiftModifier:
            h.setValue(h.value() - dy)
        else:
            h.setValue(h.value() - e.angleDelta().x())
            v.setValue(v.value() - dy)
        e.accept()

    # ================================================================== painting
    def drawBackground(self, p: QPainter, rect: QRectF):
        th = self.Theme
        p.fillRect(rect, Qc(th.Background))
        if not self.GridVisible or self._zoom < 0.2:
            return
        gs = self.GridSize
        left, top = int(math.floor(rect.left() / gs)), int(math.floor(rect.top() / gs))
        right, bottom = int(math.ceil(rect.right() / gs)), int(math.ceil(rect.bottom() / gs))
        minor, major = [], []
        for i in range(left, right + 1):
            (major if i % 5 == 0 else minor).append(QLineF(i * gs, rect.top(), i * gs, rect.bottom()))
        for j in range(top, bottom + 1):
            (major if j % 5 == 0 else minor).append(QLineF(rect.left(), j * gs, rect.right(), j * gs))
        p.setRenderHint(QPainter.Antialiasing, False)
        if self._zoom >= 0.5:
            p.setPen(QPen(Qc(th.GridMinor), 0))
            p.drawLines(minor)
        p.setPen(QPen(Qc(th.GridMajor), 0))
        p.drawLines(major)

    def drawForeground(self, p: QPainter, rect: QRectF):
        if self._band is not None:
            r = QRectF(self._band["start"], self._band["cur"]).normalized()
            c = Qc(self.Theme.RubberBandColor)
            fill = QColor(c)
            fill.setAlpha(40)
            p.setPen(QPen(c, 1.2 / self._zoom))
            p.setBrush(fill)
            p.drawRect(r)

    # ================================================================== mouse
    def mousePressEvent(self, e):
        self.setFocus()
        if e.button() == Qt.MiddleButton or (e.button() == Qt.LeftButton and self._space):
            self._pan = e.position()
            self.setCursor(Qt.ClosedHandCursor)
            e.accept()
            return
        if e.button() != Qt.LeftButton:
            e.accept()
            return
        sp = self.mapToScene(e.position().toPoint())
        mods = e.modifiers()
        item = self.BlockAt(sp)
        multi = bool(mods & (Qt.ControlModifier | Qt.ShiftModifier))
        if item is None:
            if not multi:
                self.Selection.Clear()
            self._band = {"start": sp, "cur": sp, "base": self.Selection.SelectedIds() if multi else set()}
            return
        blk = item.Block
        hot = item.HotAt(item.mapFromScene(sp))
        was = self.Selection.IsSelected(blk)
        if not was:
            if multi:
                self.Selection.Add([blk])
            else:
                self.Selection.Set([blk])
        elif not multi:
            if any(self.Selection.IsSelected(a) for a in blk.Ancestors()):
                self.Selection.Set([blk])
        self._press = {"item": item, "scene": sp, "view": e.position(), "hot": hot, "mods": mods,
                       "was": was, "multi": multi, "started": False}

    def mouseMoveEvent(self, e):
        if self._pan is not None:
            d = e.position() - self._pan
            self._pan = e.position()
            h, v = self.horizontalScrollBar(), self.verticalScrollBar()
            h.setValue(h.value() - int(d.x()))
            v.setValue(v.value() - int(d.y()))
            return
        sp = self.mapToScene(e.position().toPoint())
        if self._band is not None:
            self._band["cur"] = sp
            self._UpdateBand()
            return
        pr = self._press
        if pr is not None and not self.ReadOnly:
            if not pr["started"]:
                if (e.position() - pr["view"]).manhattanLength() >= QApplication.startDragDistance():
                    pr["started"] = True
                    self._StartDrag(pr)
            if self._drag is not None:
                self._UpdateDrag(sp)

    def mouseReleaseEvent(self, e):
        if self._pan is not None and e.button() in (Qt.MiddleButton, Qt.LeftButton):
            self._pan = None
            self.unsetCursor()
            return
        if e.button() != Qt.LeftButton:
            return
        if self._band is not None:
            self._band = None
            self.viewport().update()
            return
        pr = self._press
        if pr is None:
            return
        if self._drag is not None:
            self._FinishDrag(e.globalPosition().toPoint())
            self._press = None
            return
        self._press = None
        self._Click(pr, e)

    def mouseDoubleClickEvent(self, e):
        sp = self.mapToScene(e.position().toPoint())
        for it in self.Scene.items(sp):
            if isinstance(it, CommentBubbleItem):
                self.Workspace.EditComment(it.Owner.Block)
                return
        super().mouseDoubleClickEvent(e)

    # ---- click handling
    def _Click(self, pr, e):
        item, blk, hot = pr["item"], pr["item"].Block, pr["hot"]
        if pr["multi"] and pr["was"]:
            self.Selection.Remove([blk])
            return
        if not pr["multi"] and pr["was"]:
            sel = self.Selection.Selected()
            if len(sel) > 1:
                self.Selection.Set([blk])
        if hot is None or self.ReadOnly:
            return
        kind, rect, payload = hot
        if kind == "field":
            self._EditField(item, payload, rect)
        elif kind == "mutator":
            menu = QMenu(self)
            self.Workspace.AddSectionActions(menu, blk)
            menu.exec(e.globalPosition().toPoint())
        elif kind == "comment":
            self.Document.SetCommentOpen(blk, not blk.CommentOpen)
        elif kind == "warning" and self.Issues.get(blk.Id):
            QToolTip.showText(e.globalPosition().toPoint(), "\n".join(i.Message for i in self.Issues[blk.Id]), self)

    def _EditField(self, item, ri, rect):
        b = item.Block
        fd = ri.Def if ri.Kind == "field" else ri.Def.Shadow
        if fd is None:
            return
        vr = self.mapFromScene(item.mapToScene(rect)).boundingRect()
        doc, key = self.Document, ri.Key

        def commit(v):
            if b.Id in doc.Blocks:
                doc.SetFieldValue(b, key, v)

        ctx = FieldEditContext(self.viewport(), vr, b.Values.get(key), commit, self.Workspace, b, key, fd)
        fd.OnClick(ctx)

    # ---- rubber band
    def _UpdateBand(self):
        r = QRectF(self._band["start"], self._band["cur"]).normalized()
        hits = []
        for it in self.Scene.items(r, Qt.IntersectsItemShape):
            if isinstance(it, BlockGraphicsItem) and it.isVisible():
                hits.append(it.Block)
        ids = set(self._band["base"])
        self.Selection.Set([self.Document.Blocks[i] for i in ids if i in self.Document.Blocks] + hits)
        self.viewport().update()

    # ================================================================== dragging
    def _StartDrag(self, pr):
        doc = self.Document
        blk = pr["item"].Block
        roots = self.Selection.DragRoots()
        if not roots:
            roots = [blk]
        heal = bool(pr["mods"] & Qt.AltModifier)
        doc.Commands.Begin("Move Blocks")
        for r in roots:
            if r.Parent is not None or (heal and r.Next is not None):
                doc.DetachForDrag(r, HealStack=heal)
        doc.Flush()
        primary = None
        for r in roots:
            cur = blk
            while cur is not None:
                if cur is r:
                    primary = r
                    break
                cur = cur.Parent
            if primary is not None:
                break
        self._BeginDragState(roots, primary or roots[0], pr["scene"], False)

    def _BeginDragState(self, roots, primary, anchor, external):
        ds = _DragState()
        ds.Roots = list(roots)
        ds.Items = [self.Items[r.Id] for r in roots]
        ds.Orig = [it.scenePos() for it in ds.Items]
        ds.Anchor = anchor
        ds.Primary = self.Items[primary.Id]
        ds.External = external
        for it in ds.Items:
            it.setZValue(1000)
        self._drag = ds
        self.setCursor(Qt.ClosedHandCursor)

    def _UpdateDrag(self, sp: QPointF):
        ds = self._drag
        delta = sp - ds.Anchor
        for it, o in zip(ds.Items, ds.Orig):
            it.setPos(o + delta)
        snap, valid = self._FindSnap(ds)
        ds.Snap = snap
        if snap is None and not valid:
            self.Preview.Clear()
            return
        pts = [v["pt"] for v in valid if v is not snap]
        ghost, best = None, None
        if snap is not None:
            prim = ds.Primary
            off = snap["pt"] - snap["mine"]
            ghost = prim.shape().translated(prim.scenePos() + off)
            best = snap["pt"]
        self.Preview.SetState(self.Theme, pts, best, ghost)

    def _FindSnap(self, ds: _DragState):
        item = ds.Primary
        blk = item.Block
        d = blk.Definition
        R = self.SnapRadius / max(self._zoom, 0.5)
        mine = []
        if d.HasPrevious:
            mine.append(("prev", item.mapToScene(QPointF(0, 0))))
        if d.IsValue:
            mine.append(("out", item.mapToScene(QPointF(0, item.Height / 2))))
        tail = blk.LastInChain()
        tail_item = self.Items.get(tail.Id)
        if tail_item is not None and tail.Definition.HasNext:
            mine.append(("tail", tail_item.mapToScene(QPointF(0, tail_item.Height))))
        if not mine:
            return None, []
        area = item.mapRectToScene(item.childrenBoundingRect().united(item.boundingRect())).adjusted(-R * 3, -R * 3, R * 3, R * 3)
        dragged = set(id(i) for i in ds.Items)
        cm = self.Connections
        valid: list = []

        def consider(parent, slot, child, tpt, kind_mine):
            for kind, mpt in mine:
                if kind != kind_mine:
                    continue
                dist = math.hypot(tpt.x() - mpt.x(), tpt.y() - mpt.y())
                if dist > R * 2.5:
                    continue
                if cm.CanConnect(parent, slot, child).Ok:
                    valid.append({"dist": dist, "parent": parent, "slot": slot, "child": child, "pt": tpt, "mine": mpt})

        for ci in self.Scene.items(area):
            if not isinstance(ci, BlockGraphicsItem) or not ci.isVisible() or id(ci.RootItem()) in dragged:
                continue
            cb = ci.Block
            cd = cb.Definition
            if cd.HasNext:
                consider(cb, NEXT, blk, ci.mapToScene(QPointF(0, ci.Height)), "prev")
            if not cb.Collapsed:
                for key, rect in ci._mouths.items():
                    consider(cb, key, blk, ci.mapToScene(rect.topLeft()), "prev")
                for key, rect in ci._sockets.items():
                    consider(cb, key, blk, ci.mapToScene(QPointF(rect.x(), rect.center().y())), "out")
            if cb.Parent is None and cd.HasPrevious and tail.Definition.HasNext:
                consider(tail, NEXT, cb, ci.mapToScene(QPointF(0, 0)), "tail")
        best = None
        for v in valid:
            if v["dist"] <= R and (best is None or v["dist"] < best["dist"]):
                best = v
        return best, valid

    def _FinishDrag(self, global_pos: QPoint):
        ds, doc = self._drag, self.Document
        self._drag = None
        self.Preview.Clear()
        self.unsetCursor()
        over_toolbox = False
        tb = getattr(self.Workspace, "Toolbox", None)
        if tb is not None and tb.isVisible():
            over_toolbox = tb.rect().contains(tb.mapFromGlobal(global_pos))
        if over_toolbox:
            doc.RemoveBlocks(ds.Roots, Heal=False)
            self.Selection.Clear()
            doc.Commands.End()
            doc.Flush()
            return
        gs = self.GridSize
        for r, it in zip(ds.Roots, ds.Items):
            p = it.pos()
            x, y = p.x(), p.y()
            if self.SnapToGrid and ds.Snap is None:
                x, y = round(x / gs) * gs, round(y / gs) * gs
            doc._SetPos(r, x, y)
            it.setZValue(0)
        connected = False
        if ds.Snap is not None:
            s = ds.Snap
            connected = self.Connections.Connect(s["parent"], s["slot"], s["child"])
        doc.Commands.Rename("Connect Blocks" if connected else ("Add Block" if ds.External else "Move Blocks"))
        doc.Commands.End()
        doc.Flush()

    def CancelDrag(self):
        if self._drag is None:
            return
        self._drag = None
        self._press = None
        self.Preview.Clear()
        self.unsetCursor()
        self.Document.Commands.Rollback()
        self.Document.Flush()

    # ---- drags that originate in the toolbox flyout (mouse grab stays with the flyout)
    def BeginExternalDrag(self, xml: str, global_pos: QPoint, grab_px: QPointF):
        doc = self.Document
        sp = self.mapToScene(self.viewport().mapFromGlobal(global_pos))
        grab = grab_px / self._zoom
        doc.Commands.Begin("Add Block")
        rep = self.Workspace.Serializer.ImportWorkspaceXml(xml, Clear=False, Undoable=True, NewIds=True,
                                                           Offset=(sp.x() - grab.x(), sp.y() - grab.y()))
        doc.Flush()
        if not rep.Roots:
            doc.Commands.Rollback()
            return
        root = rep.Roots[0]
        self.Selection.Set([root])
        self._BeginDragState([root], root, sp, True)

    def ExternalDragMove(self, global_pos: QPoint):
        if self._drag is not None:
            self._UpdateDrag(self.mapToScene(self.viewport().mapFromGlobal(global_pos)))

    def ExternalDragRelease(self, global_pos: QPoint):
        if self._drag is not None:
            self._FinishDrag(global_pos)

    # ================================================================== keys / menus
    def keyPressEvent(self, e):
        k = e.key()
        if k == Qt.Key_Space and not e.isAutoRepeat():
            self._space = True
            self.setCursor(Qt.OpenHandCursor)
            return
        if k == Qt.Key_Escape:
            if self._drag is not None:
                self.CancelDrag()
            else:
                self.Selection.Clear()
            return
        if k == Qt.Key_F and not e.modifiers():
            self.ZoomToFit()
            return
        if k in (Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down) and not self.ReadOnly:
            step = 10 if e.modifiers() & Qt.ShiftModifier else 1
            dx = {Qt.Key_Left: -step, Qt.Key_Right: step}.get(k, 0)
            dy = {Qt.Key_Up: -step, Qt.Key_Down: step}.get(k, 0)
            roots = [b for b in self.Selection.DragRoots() if b.Parent is None]
            if roots:
                with self.Document.Transaction("Nudge Blocks", MergeKey=("nudge",)):
                    for b in roots:
                        self.Document._SetPos(b, b.X + dx, b.Y + dy)
            return
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if e.key() == Qt.Key_Space and not e.isAutoRepeat():
            self._space = False
            self.unsetCursor()
            return
        super().keyReleaseEvent(e)

    def contextMenuEvent(self, e):
        if self._drag is not None:
            return
        sp = self.mapToScene(e.pos())
        item = self.BlockAt(sp)
        if item is not None:
            if not self.Selection.IsSelected(item.Block):
                self.Selection.Set([item.Block])
            menu = self.Workspace.BuildBlockMenu(item.Block, self)
        else:
            menu = self.Workspace.BuildWorkspaceMenu(sp, self)
        self.Workspace.MenuAboutToShow.emit(menu, item.Block if item is not None else None)
        menu.exec(e.globalPos())
