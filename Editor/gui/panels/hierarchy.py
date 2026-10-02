"""Project/scene hierarchy tree panel."""

from __future__ import annotations

from PySide6.QtCore import QMimeData, Qt, Signal, QTimer
from PySide6.QtGui import QAction, QColor, QIcon, QKeySequence, QPainter, QPen
from PySide6.QtWidgets import QAbstractItemView, QLineEdit, QMenu, QTreeWidget, QTreeWidgetItem, QTreeWidgetItemIterator, QVBoxLayout, QWidget

from ...localization import LocalizationManager


class HierarchyTree(QTreeWidget):
    ReparentRequested = Signal(str, str)
    AssetDropped = Signal(str, str)
    BackgroundClicked = Signal()

    def __init__(self,parent=None)->None:
        super().__init__(parent);self._drop_highlight=None

    def _SetDropHighlight(self,item)->None:
        if item is self._drop_highlight:return
        self._drop_highlight=item;self.viewport().update()

    def _ClearDropHighlight(self)->None:
        self._drop_highlight=None;self.viewport().update()

    def paintEvent(self,event)->None:  # type: ignore[no-untyped-def]
        super().paintEvent(event)
        if self._drop_highlight is None:return
        rect=self.visualItemRect(self._drop_highlight)
        if not rect.isValid():return
        color=self.palette().highlight().color();fill=QColor(color);fill.setAlpha(52)
        painter=QPainter(self.viewport());painter.fillRect(rect,fill);painter.setPen(QPen(color,2));painter.drawRect(rect.adjusted(1,1,-2,-2));painter.end()

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
        target=self.itemAt(event.position().toPoint())
        self._SetDropHighlight(target);event.setDropAction(Qt.DropAction.CopyAction);event.accept()

    def dragLeaveEvent(self,event)->None:  # type: ignore[no-untyped-def]
        self._ClearDropHighlight();super().dragLeaveEvent(event)

    def dropEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        target = self.itemAt(event.position().toPoint())
        self._ClearDropHighlight()
        parent = "" if target is None else str(target.data(0, Qt.ItemDataRole.UserRole) or "")
        if event.mimeData().hasFormat("application/x-bazzalt-entity"):
            source=bytes(event.mimeData().data("application/x-bazzalt-entity")).decode()
            # The scene owns the move.  Report CopyAction to Qt so its internal
            # model drag code does not remove the freshly rebuilt source row.
            if source and source!=parent:self.ReparentRequested.emit(source,parent);event.setDropAction(Qt.DropAction.CopyAction);event.accept()
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
    RenameRequested = Signal(str, str)
    CopyRequested = Signal(object)
    PasteRequested = Signal(object)
    ActivateSceneRequested = Signal(str)
    UnloadSceneRequested = Signal(str)
    RenameSceneRequested = Signal(str, str)

    def __init__(self, localization: LocalizationManager, install_shortcuts: bool = True) -> None:
        super().__init__(); self._localization = localization;self._collapsed_ids:set[str]=set()
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0)
        self.Search=QLineEdit();self.Search.setObjectName("HierarchySearch");self.Search.setClearButtonEnabled(True);layout.addWidget(self.Search)
        self._search_expansion=None;self._filter_timer=QTimer(self);self._filter_timer.setSingleShot(True);self._filter_timer.timeout.connect(self.ApplySearch)
        self.Tree=HierarchyTree();self.Tree.setHeaderHidden(True);self.Tree.setRootIsDecorated(True);self.Tree.setItemsExpandable(True);self.Tree.setIndentation(14);self.Tree.setUniformRowHeights(True);self.Tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);self.Tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.Tree.setDragEnabled(True);self.Tree.setAcceptDrops(True);self.Tree.setDropIndicatorShown(True);self.Tree.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop);self.Tree.setDefaultDropAction(Qt.DropAction.CopyAction);self.Tree.setSupportedDragActions(Qt.DropAction.CopyAction)
        self.Tree.ReparentRequested.connect(self.ReparentRequested)
        self.Tree.AssetDropped.connect(self.AssetDropped)
        self.Tree.BackgroundClicked.connect(lambda:self.SelectionChanged.emit(None))
        self.Tree.customContextMenuRequested.connect(self._ShowContextMenu)
        self.Tree.itemSelectionChanged.connect(self._SelectionChanged)
        self.Tree.itemChanged.connect(self._ItemRenamed)
        self.Tree.itemCollapsed.connect(self._RememberCollapsed);self.Tree.itemExpanded.connect(self._RememberExpanded)
        if install_shortcuts:self._InstallActions()
        layout.addWidget(self.Tree)
        self.Search.textChanged.connect(self.ApplySearch)
        localization.LocaleChanged.connect(lambda _:self._RetranslateSearch());self._RetranslateSearch()

    def _RetranslateSearch(self)->None:
        self.Search.setPlaceholderText(self._localization.Translate("hierarchy.search"));self.Search.setToolTip(self._localization.Translate("hierarchy.search_hint"))

    def ApplySearch(self,*_args)->None:
        terms=self.Search.text().casefold().split();items=[];iterator=QTreeWidgetItemIterator(self.Tree)
        while iterator.value() is not None:items.append(iterator.value());iterator+=1
        if terms and self._search_expansion is None:self._search_expansion={str(item.data(0,Qt.ItemDataRole.UserRole)):item.isExpanded() for item in items}
        blocked=self.Tree.blockSignals(True)
        def visit(item,ancestor_matches=False):
            own=bool(terms) and all(term in item.text(0).casefold() for term in terms)
            children=[visit(item.child(index),ancestor_matches or own) for index in range(item.childCount())]
            visible=not terms or own or ancestor_matches or any(children);item.setHidden(not visible)
            if terms and any(children):item.setExpanded(True)
            elif not terms and self._search_expansion is not None:
                value=str(item.data(0,Qt.ItemDataRole.UserRole));item.setExpanded(self._search_expansion.get(value,item.data(0,Qt.ItemDataRole.UserRole+1)=="scene" or value not in self._collapsed_ids))
            return visible
        for index in range(self.Tree.topLevelItemCount()):visit(self.Tree.topLevelItem(index))
        if not terms:self._search_expansion=None
        self.Tree.blockSignals(blocked)

    def AddItem(self, name: str, data=None, parent: QTreeWidgetItem | None = None,
                icon: QIcon | None = None, kind: str = "entity", active: bool = False) -> QTreeWidgetItem:
        item=QTreeWidgetItem([name]);item.setData(0,Qt.ItemDataRole.UserRole,data)
        item.setData(0,Qt.ItemDataRole.UserRole+1,kind)
        item.setData(0,Qt.ItemDataRole.UserRole+2,name)
        if kind=="entity":item.setFlags(item.flags()|Qt.ItemFlag.ItemIsEditable|Qt.ItemFlag.ItemIsDragEnabled|Qt.ItemFlag.ItemIsDropEnabled)
        elif kind=="scene":item.setFlags(item.flags()|Qt.ItemFlag.ItemIsEditable|Qt.ItemFlag.ItemIsDropEnabled)
        if kind=="scene" and active:
            font=item.font(0);font.setBold(True);item.setFont(0,font);item.setData(0,Qt.ItemDataRole.UserRole+3,True)
        if icon is not None:item.setIcon(0,icon)
        (parent.addChild(item) if parent else self.Tree.addTopLevelItem(item))
        if self.Search.text().strip():self._filter_timer.start(0)
        return item

    def Clear(self) -> None: self.Tree.clear()
    def SetDirtyScenes(self,scene_ids)->None:
        dirty={str(value) for value in scene_ids};blocked=self.Tree.blockSignals(True)
        for index in range(self.Tree.topLevelItemCount()):
            item=self.Tree.topLevelItem(index)
            if item.data(0,Qt.ItemDataRole.UserRole+1)!="scene":continue
            base=str(item.data(0,Qt.ItemDataRole.UserRole+2) or item.text(0)).removesuffix(" *")
            item.setData(0,Qt.ItemDataRole.UserRole+2,base);item.setText(0,base+(" *" if str(item.data(0,Qt.ItemDataRole.UserRole)) in dirty else ""))
        self.Tree.blockSignals(blocked)
    def ExpandedData(self)->set[str]:
        values=set();iterator=QTreeWidgetItemIterator(self.Tree)
        while iterator.value() is not None:
            item=iterator.value()
            if item.isExpanded():values.add(str(item.data(0,Qt.ItemDataRole.UserRole) or ""))
            iterator+=1
        return values
    def ExpandData(self,value)->None:
        self._collapsed_ids.discard(str(value or ""))
        iterator=QTreeWidgetItemIterator(self.Tree)
        while iterator.value() is not None:
            item=iterator.value()
            if str(item.data(0,Qt.ItemDataRole.UserRole) or "")==str(value or ""):item.setExpanded(True);self.Tree.scrollToItem(item);return
            iterator+=1
    def ApplyExpansionState(self,items:dict[str,QTreeWidgetItem])->None:
        for value,item in items.items():
            if item.childCount():item.setExpanded(str(value) not in self._collapsed_ids)
    def _RememberCollapsed(self,item)->None:
        if self.Search.text().strip():return
        if item.data(0,Qt.ItemDataRole.UserRole+1)=="entity":self._collapsed_ids.add(str(item.data(0,Qt.ItemDataRole.UserRole)))
    def _RememberExpanded(self,item)->None:
        if self.Search.text().strip():return
        self._collapsed_ids.discard(str(item.data(0,Qt.ItemDataRole.UserRole) or ""))
    def GetSelectedData(self):
        values=[item.data(0,Qt.ItemDataRole.UserRole) for item in self.Tree.selectedItems() if item.data(0,Qt.ItemDataRole.UserRole+1)=="entity"]
        return values
    def SetSelectedData(self,values)->None:
        wanted={str(value) for value in values};blocked=self.Tree.blockSignals(True);matches=[];items=[]
        iterator=QTreeWidgetItemIterator(self.Tree)
        while iterator.value() is not None:
            item=iterator.value();items.append(item)
            if item.data(0,Qt.ItemDataRole.UserRole+1)=="entity" and str(item.data(0,Qt.ItemDataRole.UserRole)) in wanted:matches.append(item)
            iterator+=1
        # setCurrentItem can clear an existing extended selection, so establish
        # the current row first and apply the complete selection afterward.
        self.Tree.clearSelection();self.Tree.setCurrentItem(matches[0] if matches else None)
        for item in items:item.setSelected(item in matches)
        self.Tree.blockSignals(blocked)

    def _SelectionChanged(self)->None:
        values=self.GetSelectedData();self.SelectionChanged.emit(values if len(values)>1 else values[0] if values else None)

    def _InstallActions(self)->None:
        for shortcut,callback in ((QKeySequence.StandardKey.Copy,lambda:self.CopyRequested.emit(self.GetSelectedData())),
                                  (QKeySequence.StandardKey.Paste,lambda:self.PasteRequested.emit(self._ContextParent())),
                                  (QKeySequence.StandardKey.Delete,lambda:self.DeleteRequested.emit(self.GetSelectedData())),
                                  (QKeySequence(Qt.Key.Key_F2),self._BeginRename)):
            action=QAction(self);action.setShortcut(shortcut);action.setShortcutContext(Qt.ShortcutContext.WidgetWithChildrenShortcut);action.triggered.connect(callback);self.addAction(action)

    def _ContextParent(self):
        item=self.Tree.currentItem()
        return item.data(0,Qt.ItemDataRole.UserRole) if item and item.data(0,Qt.ItemDataRole.UserRole+1) in {"entity","scene"} else None

    def _BeginRename(self)->None:
        item=self.Tree.currentItem()
        if item is not None and item.data(0,Qt.ItemDataRole.UserRole+1) in {"entity","scene"}:self.Tree.editItem(item,0)

    def _ItemRenamed(self,item,column)->None:
        if column!=0:return
        kind=item.data(0,Qt.ItemDataRole.UserRole+1)
        if kind not in {"entity","scene"}:return
        old=str(item.data(0,Qt.ItemDataRole.UserRole+2) or "");name=item.text(0).strip()
        if not name:item.setText(0,old);return
        if name!=old:
            item.setData(0,Qt.ItemDataRole.UserRole+2,name)
            (self.RenameSceneRequested if kind=="scene" else self.RenameRequested).emit(str(item.data(0,Qt.ItemDataRole.UserRole)),name)

    def _ShowContextMenu(self, position) -> None:  # type: ignore[no-untyped-def]
        item=self.Tree.itemAt(position);menu=QMenu(self)
        scene_id=str(item.data(0,Qt.ItemDataRole.UserRole) or "") if item and item.data(0,Qt.ItemDataRole.UserRole+1)=="scene" else ""
        scene_active=bool(item.data(0,Qt.ItemDataRole.UserRole+3)) if scene_id else False
        parent = item.data(0,Qt.ItemDataRole.UserRole) if item and item.data(0,Qt.ItemDataRole.UserRole+1) in {"entity","scene"} else None
        create_menu=menu.addMenu(self._localization.Translate("hierarchy.add_new"))
        for title,kind in ((self._localization.Translate("hierarchy.empty"),"Entity"),("Camera","Camera"),("Light","Light")):
            action=create_menu.addAction(title);action.triggered.connect(lambda _=False,k=kind:self.CreateTypedRequested.emit(k,parent))
        primitives=create_menu.addMenu(self._localization.Translate("hierarchy.primitives"))
        for index,key in enumerate(("cube","sphere","cylinder","capsule","plane","cone","torus")):
            action=primitives.addAction(self._localization.Translate(f"primitive.{key}"));action.triggered.connect(lambda _=False,i=index:self.CreateTypedRequested.emit(f"Primitive Object:{i}",parent))
        menu.addSeparator();rename=menu.addAction(self._localization.Translate("hierarchy.rename"));rename.setShortcut(QKeySequence(Qt.Key.Key_F2));rename.setEnabled(item is not None and item.data(0,Qt.ItemDataRole.UserRole+1) in {"entity","scene"});rename.triggered.connect(lambda:self.Tree.editItem(item,0) if item else None)
        copy=menu.addAction(self._localization.Translate("hierarchy.copy"));copy.setShortcut(QKeySequence.StandardKey.Copy);copy.setEnabled(bool(self.GetSelectedData()));copy.triggered.connect(lambda:self.CopyRequested.emit(self.GetSelectedData()))
        paste=menu.addAction(self._localization.Translate("hierarchy.paste"));paste.setShortcut(QKeySequence.StandardKey.Paste);paste.triggered.connect(lambda:self.PasteRequested.emit(parent))
        menu.addSeparator()
        if scene_id:
            activate=menu.addAction(self._localization.Translate("hierarchy.activate_scene"));activate.setEnabled(not scene_active and len(self.Tree.selectedItems())==1);activate.triggered.connect(lambda:self.ActivateSceneRequested.emit(scene_id))
            unload=menu.addAction(self._localization.Translate("hierarchy.unload_scene"));unload.setEnabled(not scene_active);unload.triggered.connect(lambda:self.UnloadSceneRequested.emit(scene_id));menu.addSeparator()
        delete=QAction(self._localization.Translate("hierarchy.delete"),menu);delete.setEnabled(item is not None and item.data(0,Qt.ItemDataRole.UserRole+1)=="entity");delete.triggered.connect(lambda:self.DeleteRequested.emit(self.GetSelectedData()));menu.addAction(delete)
        self.ContextMenuRequested.emit(menu,self.Tree.mapToGlobal(position));menu.exec(self.Tree.mapToGlobal(position))


__all__ = ["HierarchyPanel"]
