"""Project/scene hierarchy tree panel."""

from __future__ import annotations

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWidgets import QAbstractItemView, QMenu, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from ...localization import LocalizationManager


class HierarchyTree(QTreeWidget):
    ReparentRequested = Signal(str, str)
    AssetDropped = Signal(str, str)
    BackgroundClicked = Signal()

    def mousePressEvent(self,event)->None:
        if event.button()==Qt.MouseButton.LeftButton and self.itemAt(event.position().toPoint()) is None:
            self.clearSelection();self.setCurrentItem(None);self.BackgroundClicked.emit()
        super().mousePressEvent(event)

    def mimeData(self, items):  # type: ignore[no-untyped-def]
        mime = QMimeData()
        if items:
            value = items[0].data(0, Qt.ItemDataRole.UserRole)
            kind = items[0].data(0, Qt.ItemDataRole.UserRole + 1)
            if kind == "entity" and value: mime.setData("application/x-bazzalt-entity", str(value).encode())
        return mime

    def dragEnterEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        if event.mimeData().hasFormat("application/x-bazzalt-entity") or event.mimeData().hasFormat("application/x-bazzalt-asset"): event.acceptProposedAction()
        else: event.ignore()

    def dragMoveEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        event.acceptProposedAction()

    def dropEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        target = self.itemAt(event.position().toPoint())
        parent = "" if target is None or target.data(0, Qt.ItemDataRole.UserRole + 1) == "scene" else str(target.data(0, Qt.ItemDataRole.UserRole) or "")
        if event.mimeData().hasFormat("application/x-bazzalt-entity"):
            source=bytes(event.mimeData().data("application/x-bazzalt-entity")).decode()
            if source and source!=parent:self.ReparentRequested.emit(source,parent);event.acceptProposedAction()
        elif event.mimeData().hasFormat("application/x-bazzalt-asset"):
            source=bytes(event.mimeData().data("application/x-bazzalt-asset")).decode()
            if source:self.AssetDropped.emit(source,parent);event.acceptProposedAction()


class HierarchyPanel(QWidget):
    SelectionChanged = Signal(object)
    CreateRequested = Signal(object)
    DeleteRequested = Signal(object)
    ContextMenuRequested = Signal(object, object)
    CreateTypedRequested = Signal(str, object)
    ReparentRequested = Signal(str, str)
    AssetDropped = Signal(str, str)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__(); self._localization = localization
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0)
        self.Tree=HierarchyTree();self.Tree.setHeaderHidden(True);self.Tree.setRootIsDecorated(True);self.Tree.setItemsExpandable(True);self.Tree.setIndentation(14);self.Tree.setUniformRowHeights(True);self.Tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);self.Tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.Tree.setDragEnabled(True);self.Tree.setAcceptDrops(True);self.Tree.setDropIndicatorShown(True);self.Tree.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop)
        self.Tree.ReparentRequested.connect(self.ReparentRequested)
        self.Tree.AssetDropped.connect(self.AssetDropped)
        self.Tree.BackgroundClicked.connect(lambda:self.SelectionChanged.emit(None))
        self.Tree.customContextMenuRequested.connect(self._ShowContextMenu)
        self.Tree.itemSelectionChanged.connect(self._SelectionChanged)
        layout.addWidget(self.Tree)

    def AddItem(self, name: str, data=None, parent: QTreeWidgetItem | None = None,
                icon: QIcon | None = None, kind: str = "entity") -> QTreeWidgetItem:
        item=QTreeWidgetItem([name]);item.setData(0,Qt.ItemDataRole.UserRole,data)
        item.setData(0,Qt.ItemDataRole.UserRole+1,kind)
        if icon is not None:item.setIcon(0,icon)
        (parent.addChild(item) if parent else self.Tree.addTopLevelItem(item));return item

    def Clear(self) -> None: self.Tree.clear()
    def GetSelectedData(self):
        values=[item.data(0,Qt.ItemDataRole.UserRole) for item in self.Tree.selectedItems() if item.data(0,Qt.ItemDataRole.UserRole+1)=="entity"]
        return values

    def _SelectionChanged(self)->None:
        values=self.GetSelectedData();self.SelectionChanged.emit(values if len(values)>1 else values[0] if values else None)

    def _ShowContextMenu(self, position) -> None:  # type: ignore[no-untyped-def]
        item=self.Tree.itemAt(position);menu=QMenu(self)
        parent = item.data(0,Qt.ItemDataRole.UserRole) if item and item.data(0,Qt.ItemDataRole.UserRole+1)=="entity" else None
        create_menu=menu.addMenu(self._localization.Translate("hierarchy.add_new"))
        for title,kind in ((self._localization.Translate("hierarchy.empty"),"Entity"),("Camera","Camera"),("Light","Light"),("Mesh","Mesh")):
            action=create_menu.addAction(title);action.triggered.connect(lambda _=False,k=kind:self.CreateTypedRequested.emit(k,parent))
        delete=QAction(self._localization.Translate("hierarchy.delete"),menu);delete.setEnabled(item is not None and item.data(0,Qt.ItemDataRole.UserRole+1)=="entity");delete.triggered.connect(lambda:self.DeleteRequested.emit(self.GetSelectedData()));menu.addAction(delete)
        self.ContextMenuRequested.emit(menu,self.Tree.mapToGlobal(position));menu.exec(self.Tree.mapToGlobal(position))


__all__ = ["HierarchyPanel"]
