"""Project/scene hierarchy tree panel."""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWidgets import QMenu, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from ...localization import LocalizationManager


class HierarchyPanel(QWidget):
    SelectionChanged = Signal(object)
    CreateRequested = Signal(object)
    DeleteRequested = Signal(object)
    ContextMenuRequested = Signal(object, object)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__(); self._localization = localization
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0)
        self.Tree=QTreeWidget();self.Tree.setHeaderHidden(True);self.Tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.Tree.customContextMenuRequested.connect(self._ShowContextMenu)
        self.Tree.currentItemChanged.connect(lambda item,_: self.SelectionChanged.emit(item.data(0,Qt.ItemDataRole.UserRole) if item else None))
        layout.addWidget(self.Tree)

    def AddItem(self, name: str, data=None, parent: QTreeWidgetItem | None = None,
                icon: QIcon | None = None) -> QTreeWidgetItem:
        item=QTreeWidgetItem([name]);item.setData(0,Qt.ItemDataRole.UserRole,data)
        if icon is not None:item.setIcon(0,icon)
        (parent.addChild(item) if parent else self.Tree.addTopLevelItem(item));return item

    def Clear(self) -> None: self.Tree.clear()
    def GetSelectedData(self):
        item=self.Tree.currentItem();return item.data(0,Qt.ItemDataRole.UserRole) if item else None

    def _ShowContextMenu(self, position) -> None:  # type: ignore[no-untyped-def]
        item=self.Tree.itemAt(position);menu=QMenu(self)
        create=QAction(self._localization.Translate("hierarchy.create"),menu);create.triggered.connect(lambda:self.CreateRequested.emit(item.data(0,Qt.ItemDataRole.UserRole) if item else None));menu.addAction(create)
        delete=QAction(self._localization.Translate("hierarchy.delete"),menu);delete.setEnabled(item is not None);delete.triggered.connect(lambda:self.DeleteRequested.emit(item.data(0,Qt.ItemDataRole.UserRole) if item else None));menu.addAction(delete)
        self.ContextMenuRequested.emit(menu,self.Tree.mapToGlobal(position));menu.exec(self.Tree.mapToGlobal(position))


__all__ = ["HierarchyPanel"]
