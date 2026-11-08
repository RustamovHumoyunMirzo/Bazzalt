"""BlockToolbox: searchable category list + flyout. Registered blocks appear automatically."""
from __future__ import annotations
from typing import Optional

from PySide6.QtCore import Qt, QPointF, QPoint, QSize, Signal, QTimer
from PySide6.QtGui import QPainter, QPixmap, QColor, QFont, QIcon
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QListWidget, QListWidgetItem,
                               QGraphicsView, QGraphicsScene, QGraphicsSimpleTextItem, QApplication, QLabel)

from .graphics import BlockViewBase, BlockGraphicsItem
from .model import BlockDocument
from .theme import Qc


def _dot_icon(color: str, size: int = 22) -> QIcon:
    pm = QPixmap(size * 2, size * 2)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(Qt.NoPen)
    p.setBrush(Qc(color))
    p.drawEllipse(4, 4, size * 2 - 8, size * 2 - 8)
    p.end()
    pm.setDevicePixelRatio(2)
    return QIcon(pm)


class FlyoutView(QGraphicsView, BlockViewBase):
    """Read-only block palette. Dragging a block hands the drag over to the workspace canvas."""
    def __init__(self, Workspace):
        QGraphicsView.__init__(self)
        self.Scene = QGraphicsScene(self)
        self.setScene(self.Scene)
        self.InitHost(BlockDocument(Workspace.Registry), Workspace.Theme, Workspace.Registry, Workspace, False)
        self.Document.Updated.connect(self.SyncItems)
        self.setRenderHints(QPainter.Antialiasing | QPainter.TextAntialiasing)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setFrameShape(QGraphicsView.NoFrame)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)
        self.Scale = 0.85
        self.scale(self.Scale, self.Scale)
        self._press = None
        self._forwarding = False
        self._cat_y: dict = {}
        self._headings: list = []

    def Rebuild(self, groups: list):
        """groups: [(heading, color, [BlockDefinition])]"""
        doc = self.Document
        for h in self._headings:
            self.Scene.removeItem(h)
        self._headings = []
        self._cat_y = {}
        with doc.Transaction("Rebuild"):
            doc.Clear()
        doc.Commands.Clear()
        blocks = []
        with doc.Transaction("Populate"):
            for heading, color, defs in groups:
                entries = []
                for d in defs:
                    entries.append(doc.CreateBlock(d.TypeId))
                blocks.append((heading, color, entries))
        M = self.Metrics
        y, x = 10.0, 18.0
        for heading, color, entries in blocks:
            if not entries:
                continue
            t = QGraphicsSimpleTextItem(self.Workspace.CategoryLabel(heading).upper())
            f = QFont(self.Theme.FontFamily)
            f.setPixelSize(11)
            f.setBold(True)
            t.setFont(f)
            t.setBrush(Qc(color))
            t.setPos(x - 4, y)
            self.Scene.addItem(t)
            self._headings.append(t)
            self._cat_y[heading] = y - 6
            y += 26
            for b in entries:
                it = self.Items[b.Id]
                if it._hat:
                    y += M.Hat
                it.setPos(x, y)
                y += it.StackHeight(b) if False else it.Height
                y += 14 + (M.NotchH if b.Definition.HasNext else 0)
            y += 10
        self.Scene.setSceneRect(0, 0, 400, y + 40)

    def CategoryY(self, name: str) -> Optional[float]:
        return self._cat_y.get(name)

    def CurrentHeading(self) -> Optional[str]:
        top = self.mapToScene(0, 8).y()
        cur = None
        for name, y in sorted(self._cat_y.items(), key=lambda kv: kv[1]):
            if y <= top + 40:
                cur = name
        return cur

    # ---- interaction
    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            sp = self.mapToScene(e.position().toPoint())
            item = None
            for it in self.Scene.items(sp):
                if isinstance(it, BlockGraphicsItem) and it.shape().contains(it.mapFromScene(sp)):
                    item = it
                    break
            if item is not None:
                root = item.RootItem()
                self._press = {"item": root, "pos": e.position()}
                return
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self._forwarding:
            self.Workspace.Canvas.ExternalDragMove(e.globalPosition().toPoint())
            return
        pr = self._press
        if pr is not None and (e.position() - pr["pos"]).manhattanLength() >= QApplication.startDragDistance():
            item = pr["item"]
            tl = self.mapFromScene(item.scenePos())
            grab = QPointF(e.position().x() - tl.x(), e.position().y() - tl.y())
            self._forwarding = True
            self._press = None
            xml = self.Workspace.Serializer.__class__(self.Document, self.Registry).ExportBlocksXml([item.Block], lambda n: False)
            self.Workspace.Canvas.BeginExternalDrag(xml, e.globalPosition().toPoint(), grab)
            if self.Workspace.Canvas._drag is None:
                self._forwarding = False
            return
        super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if self._forwarding:
            self._forwarding = False
            self.Workspace.Canvas.ExternalDragRelease(e.globalPosition().toPoint())
            return
        self._press = None
        super().mouseReleaseEvent(e)

    def mouseDoubleClickEvent(self, e):
        sp = self.mapToScene(e.position().toPoint())
        for it in self.Scene.items(sp):
            if isinstance(it, BlockGraphicsItem):
                root = it.RootItem()
                xml = self.Workspace.Serializer.__class__(self.Document, self.Registry).ExportBlocksXml([root.Block], lambda n: False)
                self.Workspace.AddBlockFromXml(xml)
                return

    def SetTheme(self, theme):
        self.SetThemeBase(theme)
        for it in self.Items.values():
            it._layout_sig = None
            it.Refresh()
        for r in self.RootItems():
            r.LayoutTree(False)


class BlockToolbox(QWidget):
    CategorySelected = Signal(str)

    def __init__(self, Workspace):
        super().__init__()
        self.Workspace = Workspace
        self.Registry = Workspace.Registry
        self.setFixedWidth(350)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        self.Search = QLineEdit()
        self.Search.setPlaceholderText("Search blocks…")
        self.Search.setClearButtonEnabled(True)
        outer.addWidget(self.Search)
        row = QHBoxLayout()
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)
        outer.addLayout(row, 1)
        self.Categories = QListWidget()
        self.Categories.setFixedWidth(124)
        self.Categories.setIconSize(QSize(22, 22))
        self.Categories.setFocusPolicy(Qt.NoFocus)
        self.Categories.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        row.addWidget(self.Categories)
        self.Flyout = FlyoutView(Workspace)
        row.addWidget(self.Flyout, 1)
        self._hidden: set = set()
        self._syncing = False
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.setInterval(0)
        self._timer.timeout.connect(self.Refresh)
        self.Registry.Changed.connect(self.ScheduleRefresh)
        self.Search.textChanged.connect(self.ScheduleRefresh)
        self.Categories.currentRowChanged.connect(self._OnRow)
        self.Flyout.verticalScrollBar().valueChanged.connect(self._OnScroll)
        self.ApplyTheme(Workspace.Theme)
        self.Refresh()

    # ---- public
    def ScheduleRefresh(self, *_):
        self._timer.start()

    def SetCategoryVisible(self, name: str, visible: bool):
        (self._hidden.discard if visible else self._hidden.add)(name)
        self.ScheduleRefresh()

    def SetSearchText(self, text: str):
        self.Search.setText(text)

    def SelectCategory(self, name: str):
        for i in range(self.Categories.count()):
            if self.Categories.item(i).data(Qt.UserRole) == name:
                self.Categories.setCurrentRow(i)
                return

    def FocusSearch(self):
        self.Search.setFocus()
        self.Search.selectAll()

    def ApplyTheme(self, th):
        self.setStyleSheet(
            f"BlockToolbox{{background:{th.ToolboxBackground};}}"
            f"QLineEdit{{background:{th.SearchBackground};color:{th.ToolboxText};border:none;border-bottom:1px solid {th.ToolboxBorder};padding:8px 10px;}}"
            f"QListWidget{{background:{th.ToolboxBackground};color:{th.ToolboxText};border:none;border-right:1px solid {th.ToolboxBorder};outline:0;}}"
            f"QListWidget::item{{padding:9px 6px;}}QListWidget::item:selected{{background:{th.ToolboxSelected};color:{th.ToolboxText};}}")
        self.Flyout.setBackgroundBrush(Qc(th.FlyoutBackground))
        self.Flyout.SetTheme(th)
        self.Refresh()

    def Refresh(self):
        reg = self.Registry
        th = self.Workspace.Theme
        text = self.Search.text().strip()
        groups = []
        if text:
            defs = [d for d in reg.Search(text) if d.Category not in self._hidden]
            groups.append((f"Search results ({len(defs)})", "#8A8A8A", defs))
            cats = []
        else:
            cats = [c for c in reg.Categories() if c.Name not in self._hidden]
            for c in cats:
                color = c.Color or th.CategoryColor(c.Name)
                groups.append((c.Name, color, reg.InCategory(c.Name)))
        self._syncing = True
        self.Categories.clear()
        for name, color, _ in groups:
            it = QListWidgetItem(_dot_icon(color), self.Workspace.CategoryLabel(name) if not text else "Results")
            it.setData(Qt.UserRole, name)
            self.Categories.addItem(it)
        self._syncing = False
        self.Flyout.Rebuild(groups)
        if self.Categories.count():
            self._syncing = True
            self.Categories.setCurrentRow(0)
            self._syncing = False

    def _OnRow(self, row):
        if self._syncing or row < 0:
            return
        name = self.Categories.item(row).data(Qt.UserRole)
        y = self.Flyout.CategoryY(name)
        if y is not None:
            self.Flyout.verticalScrollBar().setValue(int(y * self.Flyout.Scale))
        self.CategorySelected.emit(name)

    def _OnScroll(self, _):
        if self._syncing:
            return
        name = self.Flyout.CurrentHeading()
        if name is None:
            return
        for i in range(self.Categories.count()):
            if self.Categories.item(i).data(Qt.UserRole) == name:
                if self.Categories.currentRow() != i:
                    self._syncing = True
                    self.Categories.setCurrentRow(i)
                    self._syncing = False
                return
