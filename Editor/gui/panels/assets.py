"""Project asset browser with safe filesystem editing and typed drag payloads."""
from __future__ import annotations
import shutil
from pathlib import Path
from PySide6.QtCore import QFile,QMimeData,QSize,Qt,Signal
from PySide6.QtGui import QColor,QIcon,QLinearGradient,QPainter,QPixmap,QRadialGradient
from PySide6.QtWidgets import (QAbstractItemView,QFileDialog,QListWidget,QListWidgetItem,QMenu,QSplitter,QTreeWidget,QTreeWidgetItem,QVBoxLayout,QWidget)
from ...localization import LocalizationManager

IMAGE_EXTENSIONS={".png",".jpg",".jpeg",".bmp",".gif",".webp"}
MODEL_EXTENSIONS={".gltf",".glb",".obj",".fbx",".dae",".filamesh"}
SHADER_EXTENSIONS={".mat",".shad",".vert",".frag",".glsl"}
ENVIRONMENT_EXTENSIONS={".hdr",".exr",".ktx",".ktx2"}

class AssetList(QListWidget):
    EmptyClicked=Signal()
    def mousePressEvent(self,event)->None:
        if event.button()==Qt.MouseButton.LeftButton and self.itemAt(event.position().toPoint()) is None:self.clearSelection();self.EmptyClicked.emit()
        super().mousePressEvent(event)
    def mimeData(self,items):
        mime=QMimeData();paths=[str(i.data(Qt.ItemDataRole.UserRole)) for i in items if i.data(Qt.ItemDataRole.UserRole)]
        if paths:mime.setData("application/x-bazzalt-asset",paths[0].encode());mime.setData("application/x-bazzalt-assets","\n".join(paths).encode())
        return mime

class AssetBrowserPanel(QWidget):
    AssetActivated=Signal(object);AssetSelected=Signal(object);ContextMenuRequested=Signal(object,object);SelectionCleared=Signal()
    def __init__(self,localization:LocalizationManager,resources=None)->None:
        super().__init__();self._localization=localization;self._resources=resources;self._root=None;self._folder=None;self._clipboard=[]
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0);splitter=QSplitter(Qt.Orientation.Horizontal);splitter.setChildrenCollapsible(False)
        self.Tree=QTreeWidget();self.Tree.setHeaderHidden(True);self.Tree.currentItemChanged.connect(self._FolderSelected)
        self.Browser=AssetList();self.Browser.setViewMode(QListWidget.ViewMode.IconMode);self.Browser.setIconSize(QSize(48,48));self.Browser.setGridSize(QSize(104,86));self.Browser.setResizeMode(QListWidget.ResizeMode.Adjust);self.Browser.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection);self.Browser.setDragEnabled(True)
        self.Browser.itemDoubleClicked.connect(self._Activate);self.Browser.itemSelectionChanged.connect(self._SelectionChanged);self.Browser.itemChanged.connect(self._ItemRenamed);self.Browser.EmptyClicked.connect(self.SelectionCleared);self.Tree.itemClicked.connect(lambda *_:self.SelectionCleared.emit())
        for widget in (self.Tree,self.Browser):widget.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);widget.customContextMenuRequested.connect(lambda pos,w=widget:self._ShowContextMenu(w,pos))
        self.Tree.setMinimumWidth(110);self.Browser.setMinimumWidth(140);splitter.addWidget(self.Tree);splitter.addWidget(self.Browser);splitter.setStretchFactor(0,1);splitter.setStretchFactor(1,3);splitter.setSizes([220,700]);layout.addWidget(splitter)
    def SetProjectRoot(self,root)->None:self._root=Path(root).resolve() if root else None;self.Refresh()
    def CurrentFolder(self):return self._folder
    def _InsideRoot(self,path):
        if self._root is None:return False
        try:Path(path).resolve().relative_to(self._root);return True
        except (OSError,ValueError):return False
    def _Icon(self,path):
        path=Path(path);ext=path.suffix.lower()
        if ext in IMAGE_EXTENSIONS:
            icon=QIcon(str(path))
            if not icon.isNull():return icon
        if ext==".matinst":
            pixmap=QPixmap(64,64);pixmap.fill(Qt.GlobalColor.transparent);painter=QPainter(pixmap);gradient=QRadialGradient(25,20,35);gradient.setColorAt(0,QColor("#f4f4f4"));gradient.setColorAt(.35,QColor("#9aa5b5"));gradient.setColorAt(1,QColor("#1d222a"));painter.setBrush(gradient);painter.setPen(QColor("#59616d"));painter.drawEllipse(7,7,50,50);painter.end();return QIcon(pixmap)
        if ext in ENVIRONMENT_EXTENSIONS:
            pixmap=QPixmap(64,64);gradient=QLinearGradient(0,0,0,64);gradient.setColorAt(0,QColor("#456f9b"));gradient.setColorAt(.55,QColor("#d4b678"));gradient.setColorAt(1,QColor("#252d25"));painter=QPainter(pixmap);painter.fillRect(pixmap.rect(),gradient);painter.end();return QIcon(pixmap)
        name="dir.svg" if path.is_dir() else "nativecpp.svg" if ext in {".h",".hpp",".c",".cc",".cpp"} else "3dfiles.svg" if ext in MODEL_EXTENSIONS else "shader.svg" if ext in SHADER_EXTENSIONS else "file.svg"
        return self._resources.Icon(f"icons/abrowser/{name}") if self._resources else QIcon()
    def Refresh(self,select=None)->None:
        self.Tree.clear();self.Browser.clear();self._folder=None
        if self._root is None or not self._root.is_dir():return
        root=QTreeWidgetItem([self._root.name]);root.setData(0,Qt.ItemDataRole.UserRole,self._root);root.setIcon(0,self._Icon(self._root));self.Tree.addTopLevelItem(root);self._PopulateDirectories(root,self._root);root.setExpanded(True);self.Tree.setCurrentItem(root)
        if select:
            for i in range(self.Browser.count()):
                item=self.Browser.item(i)
                if Path(item.data(Qt.ItemDataRole.UserRole))==Path(select):item.setSelected(True);self.Browser.setCurrentItem(item);break
    def _PopulateDirectories(self,parent,path)->None:
        try:entries=sorted((p for p in Path(path).iterdir() if p.is_dir() and not p.is_symlink()),key=lambda p:p.name.lower())
        except OSError:return
        for directory in entries:
            item=QTreeWidgetItem([directory.name]);item.setData(0,Qt.ItemDataRole.UserRole,directory);item.setIcon(0,self._Icon(directory));parent.addChild(item);self._PopulateDirectories(item,directory)
    def _FolderSelected(self,item,_previous)->None:
        self.Browser.blockSignals(True);self.Browser.clear();self.Browser.blockSignals(False);self._folder=Path(item.data(0,Qt.ItemDataRole.UserRole)) if item else None
        if self._folder is None:return
        try:entries=sorted(self._folder.iterdir(),key=lambda p:(not p.is_dir(),p.name.lower()))
        except OSError:return
        for entry in entries:
            if entry.name.endswith(".meta"):continue
            asset=QListWidgetItem(self._Icon(entry),entry.name);asset.setData(Qt.ItemDataRole.UserRole,entry);asset.setData(Qt.ItemDataRole.UserRole+1,entry.name);asset.setFlags(asset.flags()|Qt.ItemFlag.ItemIsEditable|Qt.ItemFlag.ItemIsDragEnabled);self.Browser.addItem(asset)
    def _SelectionChanged(self)->None:
        items=self.Browser.selectedItems()
        if len(items)==1:self.AssetSelected.emit(items[0].data(Qt.ItemDataRole.UserRole))
        elif not items:self.SelectionCleared.emit()
    def _Activate(self,item)->None:
        path=Path(item.data(Qt.ItemDataRole.UserRole))
        if path.is_dir():
            for match in self.Tree.findItems(path.name,Qt.MatchFlag.MatchExactly|Qt.MatchFlag.MatchRecursive):
                if Path(match.data(0,Qt.ItemDataRole.UserRole))==path:self.Tree.setCurrentItem(match);return
        self.AssetActivated.emit(path)
    def _Unique(self,name):
        candidate=Path(self._folder or self._root)/name;stem,suffix=candidate.stem,candidate.suffix;number=1
        while candidate.exists():candidate=candidate.with_name(f"{stem} {number}{suffix}");number+=1
        return candidate
    def _Create(self,kind)->None:
        names={"folder":"New Folder","cpp":"NewComponent.cpp","shader":"NewShader.shad","material":"NewMaterial.matinst"};path=self._Unique(names[kind])
        if kind=="folder":path.mkdir()
        else:path.write_text("#include <Bazzalt/Component.h>\n\n// COMPONENT(NewComponent)\n" if kind=="cpp" else "shader NewShader {\n}\n" if kind=="shader" else '{\n  "shader": null,\n  "properties": {}\n}\n',encoding="utf-8")
        self.Refresh(path);self.Browser.editItem(self.Browser.currentItem())
    def _Import(self)->None:
        if self._folder is None:return
        dialog=QFileDialog(self,self._localization.Translate("assets.import"));dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFiles)
        if not dialog.exec():return
        for value in dialog.selectedFiles():
            try:shutil.copy2(Path(value),self._Unique(Path(value).name))
            except OSError:pass
        self.Refresh()
    def _ItemRenamed(self,item)->None:
        old=Path(item.data(Qt.ItemDataRole.UserRole));name=item.text().strip()
        if not name or name==item.data(Qt.ItemDataRole.UserRole+1):return
        target=old.with_name(name)
        if not self._InsideRoot(target) or target.exists():item.setText(old.name);return
        try:old.rename(target)
        except OSError:item.setText(old.name);return
        meta=old.with_name(old.name+".meta")
        if meta.exists():
            try:meta.rename(target.with_name(target.name+".meta"))
            except OSError:pass
        self.Refresh(target)
    def _Delete(self)->None:
        for item in self.Browser.selectedItems():
            path=Path(item.data(Qt.ItemDataRole.UserRole))
            if self._InsideRoot(path):QFile.moveToTrash(str(path));meta=path.with_name(path.name+".meta");QFile.moveToTrash(str(meta)) if meta.exists() else None
        self.Refresh();self.SelectionCleared.emit()
    def _Copy(self)->None:self._clipboard=[Path(i.data(Qt.ItemDataRole.UserRole)) for i in self.Browser.selectedItems()]
    def _Paste(self)->None:
        if self._folder is None:return
        for source in self._clipboard:
            if not source.exists() or not self._InsideRoot(source):continue
            try:shutil.copytree(source,self._Unique(source.name)) if source.is_dir() else shutil.copy2(source,self._Unique(source.name))
            except OSError:pass
        self.Refresh()
    def _ShowContextMenu(self,widget,position)->None:
        tr=self._localization.Translate;menu=QMenu(self);create=menu.addMenu(tr("assets.create"))
        for key,label in (("folder","assets.folder"),("cpp","assets.native_cpp"),("shader","assets.shader"),("material","assets.material")):
            action=create.addAction(tr(label));action.triggered.connect(lambda _=False,k=key:self._Create(k))
        menu.addAction(tr("assets.import"),self._Import);menu.addSeparator();selected=bool(self.Browser.selectedItems()) if widget is self.Browser else False
        rename=menu.addAction(tr("assets.rename"));rename.setEnabled(len(self.Browser.selectedItems())==1);rename.triggered.connect(lambda:self.Browser.editItem(self.Browser.currentItem()));copy=menu.addAction(tr("assets.copy"));copy.setEnabled(selected);copy.triggered.connect(self._Copy);paste=menu.addAction(tr("assets.paste"));paste.setEnabled(bool(self._clipboard));paste.triggered.connect(self._Paste);delete=menu.addAction(tr("assets.delete"));delete.setEnabled(selected);delete.triggered.connect(self._Delete);menu.addSeparator();menu.addAction(tr("assets.refresh"),self.Refresh)
        self.ContextMenuRequested.emit(menu,widget.mapToGlobal(position));menu.exec(widget.mapToGlobal(position))

__all__=["AssetBrowserPanel","IMAGE_EXTENSIONS","MODEL_EXTENSIONS","SHADER_EXTENSIONS","ENVIRONMENT_EXTENSIONS"]
