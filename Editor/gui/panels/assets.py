"""Two-pane project asset browser."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QMimeData, QSize, Qt, Signal
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QListWidget, QListWidgetItem, QMenu, QSplitter, QTreeWidget, QTreeWidgetItem,
    QVBoxLayout, QWidget,
)

from ...localization import LocalizationManager


class AssetList(QListWidget):
    def mimeData(self, items):  # type: ignore[no-untyped-def]
        mime = QMimeData()
        if items:
            path = items[0].data(Qt.ItemDataRole.UserRole)
            if path: mime.setData("application/x-bazzalt-asset", str(path).encode("utf-8"))
        return mime


class AssetBrowserPanel(QWidget):
    AssetActivated = Signal(object)
    ContextMenuRequested = Signal(object, object)

    def __init__(self, localization: LocalizationManager) -> None:
        super().__init__();self._localization=localization;self._root:Path|None=None
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0)
        splitter=QSplitter(Qt.Orientation.Horizontal);splitter.setChildrenCollapsible(False)
        self.Tree=QTreeWidget();self.Tree.setHeaderHidden(True);self.Tree.currentItemChanged.connect(self._FolderSelected)
        self.Browser=AssetList();self.Browser.setViewMode(QListWidget.ViewMode.IconMode);self.Browser.setIconSize(QSize(48,48));self.Browser.setGridSize(QSize(100,82));self.Browser.setResizeMode(QListWidget.ResizeMode.Adjust);self.Browser.setDragEnabled(True)
        self.Browser.itemDoubleClicked.connect(lambda item:self.AssetActivated.emit(item.data(Qt.ItemDataRole.UserRole)))
        for widget in (self.Tree,self.Browser):widget.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);widget.customContextMenuRequested.connect(lambda pos,w=widget:self._ShowContextMenu(w,pos))
        self.Tree.setMinimumWidth(110);self.Browser.setMinimumWidth(140)
        splitter.addWidget(self.Tree);splitter.addWidget(self.Browser);splitter.setStretchFactor(0,1);splitter.setStretchFactor(1,3);splitter.setSizes([220,700]);layout.addWidget(splitter)

    def SetProjectRoot(self, root: str | Path | None) -> None:
        self._root=Path(root).resolve() if root else None;self.Refresh()

    def Refresh(self) -> None:
        self.Tree.clear();self.Browser.clear()
        if self._root is None or not self._root.is_dir():return
        root_item=QTreeWidgetItem([self._root.name]);root_item.setData(0,Qt.ItemDataRole.UserRole,self._root);self.Tree.addTopLevelItem(root_item)
        self._PopulateDirectories(root_item,self._root);root_item.setExpanded(True);self.Tree.setCurrentItem(root_item)

    def _PopulateDirectories(self, parent: QTreeWidgetItem, path: Path) -> None:
        try: entries=sorted((p for p in path.iterdir() if p.is_dir() and not p.is_symlink()),key=lambda p:p.name.lower())
        except OSError:return
        for directory in entries:
            item=QTreeWidgetItem([directory.name]);item.setData(0,Qt.ItemDataRole.UserRole,directory);parent.addChild(item);self._PopulateDirectories(item,directory)

    def _FolderSelected(self, item: QTreeWidgetItem | None, _previous) -> None:  # type: ignore[no-untyped-def]
        self.Browser.clear()
        if item is None:return
        path=item.data(0,Qt.ItemDataRole.UserRole)
        try: entries=sorted(Path(path).iterdir(),key=lambda p:(not p.is_dir(),p.name.lower()))
        except OSError:return
        for entry in entries:
            if entry.name.endswith(".meta"):continue
            asset=QListWidgetItem(entry.name);asset.setData(Qt.ItemDataRole.UserRole,entry);self.Browser.addItem(asset)

    def _ShowContextMenu(self, widget: QWidget, position) -> None:  # type: ignore[no-untyped-def]
        menu=QMenu(self);refresh=QAction(self._localization.Translate("assets.refresh"),menu);refresh.triggered.connect(self.Refresh);menu.addAction(refresh)
        self.ContextMenuRequested.emit(menu,widget.mapToGlobal(position));menu.exec(widget.mapToGlobal(position))


__all__ = ["AssetBrowserPanel"]
