"""Project asset browser with safe filesystem editing and typed drag payloads."""
from __future__ import annotations
import shutil
import os
import sys
import uuid
from pathlib import Path
from PySide6.QtCore import QFile,QFileSystemWatcher,QMimeData,QSize,Qt,Signal,QTimer
from PySide6.QtGui import QColor,QIcon,QLinearGradient,QPainter,QPixmap,QRadialGradient
from PySide6.QtWidgets import (QAbstractItemView,QFileDialog,QLineEdit,QListWidget,QListWidgetItem,QMenu,QSplitter,QStyledItemDelegate,QTreeWidget,QTreeWidgetItem,QVBoxLayout,QWidget)
from ...localization import LocalizationManager
from ...platform_services import RevealFiles

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

class AssetNameDelegate(QStyledItemDelegate):
    """Keeps compact labels in the grid while exposing the real name for edits."""
    def setEditorData(self,editor,index)->None:
        path=index.data(Qt.ItemDataRole.UserRole)
        if isinstance(editor,QLineEdit) and path:
            editor.setText(Path(path).name);editor.selectAll();return
        super().setEditorData(editor,index)

class AssetBrowserPanel(QWidget):
    AssetsChanged=Signal()
    AssetActivated=Signal(object);AssetSelected=Signal(object);ContextMenuRequested=Signal(object,object);SelectionCleared=Signal();LoadSceneRequested=Signal(object);AssetOperationFailed=Signal(str)
    def __init__(self,localization:LocalizationManager,resources=None)->None:
        super().__init__();self._localization=localization;self._resources=resources;self._root=None;self._folder=None;self._clipboard=[];self.SceneRenameHandler=None;self.SceneLoadedChecker=None;self.ExternalOpener=None
        self._watcher=QFileSystemWatcher(self);self._watcher.directoryChanged.connect(lambda _path:self.Refresh())
        layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0)
        self.Search=QLineEdit();self.Search.setObjectName("AssetBrowserSearch");self.Search.setClearButtonEnabled(True);layout.addWidget(self.Search)
        self._search_timer=QTimer(self);self._search_timer.setSingleShot(True);self._search_timer.setInterval(150);self._search_timer.timeout.connect(self._PopulateBrowser)
        self.Search.textChanged.connect(lambda _:self._search_timer.start())
        localization.LocaleChanged.connect(lambda _:self._RetranslateSearch());self._RetranslateSearch()
        splitter=QSplitter(Qt.Orientation.Horizontal);splitter.setChildrenCollapsible(False)
        self.Tree=QTreeWidget();self.Tree.setHeaderHidden(True);self.Tree.currentItemChanged.connect(self._FolderSelected)
        self.Browser=AssetList();self.Browser.setItemDelegate(AssetNameDelegate(self.Browser));self.Browser.setViewMode(QListWidget.ViewMode.IconMode);self.Browser.setIconSize(QSize(48,48));self.Browser.setGridSize(QSize(104,86));self.Browser.setResizeMode(QListWidget.ResizeMode.Adjust);self.Browser.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.Browser.setUniformItemSizes(True);self.Browser.setWordWrap(False);self.Browser.setTextElideMode(Qt.TextElideMode.ElideMiddle);self.Browser.setSpacing(2);self.Browser.setMovement(QListWidget.Movement.Static)
        # QListView::setMovement resets drag/drop mode, so drag-source setup
        # must be applied after all icon-layout configuration.
        self.Browser.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly);self.Browser.setDragEnabled(True);self.Browser.setDefaultDropAction(Qt.DropAction.CopyAction);self.Browser.setSupportedDragActions(Qt.DropAction.CopyAction)
        self.Browser.itemDoubleClicked.connect(self._Activate);self.Browser.itemSelectionChanged.connect(self._SelectionChanged);self.Browser.itemChanged.connect(self._ItemRenamed);self.Browser.EmptyClicked.connect(self.SelectionCleared);self.Tree.itemClicked.connect(self._TreeSelected)
        for widget in (self.Tree,self.Browser):widget.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);widget.customContextMenuRequested.connect(lambda pos,w=widget:self._ShowContextMenu(w,pos))
        self.Tree.setMinimumWidth(110);self.Browser.setMinimumWidth(140);splitter.addWidget(self.Tree);splitter.addWidget(self.Browser);splitter.setStretchFactor(0,1);splitter.setStretchFactor(1,3);splitter.setSizes([220,700]);layout.addWidget(splitter)
    def SetProjectRoot(self,root)->None:
        if self._watcher.directories():self._watcher.removePaths(self._watcher.directories())
        self._root=Path(root).resolve() if root else None
        if self._root and self._root.is_dir():self._watcher.addPath(str(self._root))
        self.Refresh()
    def CurrentFolder(self):return self._folder
    def _RetranslateSearch(self)->None:
        self.Search.setPlaceholderText(self._localization.Translate("assets.search"));self.Search.setToolTip(self._localization.Translate("assets.search_hint"))
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
        name="dir.svg" if path.is_dir() else "scene.svg" if ext==".bscene" else "nativecpp.svg" if ext in {".h",".hpp",".c",".cc",".cpp"} else "3dfiles.svg" if ext in MODEL_EXTENSIONS else "shader.svg" if ext in SHADER_EXTENSIONS else "file.svg"
        return self._resources.Icon(f"icons/abrowser/{name}") if self._resources else QIcon()
    def Refresh(self,select=None)->None:
        self.AssetsChanged.emit()
        previous_folder=self._folder if self._folder is not None and self._InsideRoot(self._folder) else self._root
        if select and self._InsideRoot(select):
            # Newly created/renamed assets must remain visible and editable,
            # even when their name no longer matches the search query.
            self.Search.clear();self._search_timer.stop();previous_folder=Path(select).parent
        self.Tree.clear();self.Browser.clear();self._folder=None
        if self._root is None or not self._root.is_dir():return
        root=QTreeWidgetItem([self._root.name]);root.setData(0,Qt.ItemDataRole.UserRole,self._root);root.setIcon(0,self._Icon(self._root));self.Tree.addTopLevelItem(root);self._PopulateDirectories(root,self._root);root.setExpanded(True)
        target=self._FindDirectoryItem(root,previous_folder) or root;self.Tree.setCurrentItem(target);self.Tree.scrollToItem(target)
        if select:
            for i in range(self.Browser.count()):
                item=self.Browser.item(i)
                if Path(item.data(Qt.ItemDataRole.UserRole))==Path(select):item.setSelected(True);self.Browser.setCurrentItem(item);break
    def _FindDirectoryItem(self,item,path):
        if path is not None and Path(item.data(0,Qt.ItemDataRole.UserRole))==Path(path):return item
        for index in range(item.childCount()):
            found=self._FindDirectoryItem(item.child(index),path)
            if found:return found
        return None
    def _PopulateDirectories(self,parent,path)->None:
        try:entries=sorted((p for p in Path(path).iterdir() if p.is_dir() and not p.is_symlink()),key=lambda p:p.name.lower())
        except OSError:return
        for directory in entries:
            item=QTreeWidgetItem([directory.name]);item.setData(0,Qt.ItemDataRole.UserRole,directory);item.setIcon(0,self._Icon(directory));parent.addChild(item);self._PopulateDirectories(item,directory)
    def _FolderSelected(self,item,_previous)->None:
        self._folder=Path(item.data(0,Qt.ItemDataRole.UserRole)) if item else None
        if self._folder is None:return
        watched=self._watcher.directories()
        for value in watched:
            if self._root is None or Path(value)!=self._root:self._watcher.removePath(value)
        if str(self._folder) not in self._watcher.directories():self._watcher.addPath(str(self._folder))
        self._PopulateBrowser()

    def _PopulateBrowser(self)->None:
        self._search_timer.stop();selected={str(item.data(Qt.ItemDataRole.UserRole)) for item in self.Browser.selectedItems()}
        blocked=self.Browser.blockSignals(True);self.Browser.clear()
        if self._folder is None:self.Browser.blockSignals(blocked);return
        terms=self.Search.text().casefold().split()
        def candidates():
            if not terms:yield from self._folder.iterdir();return
            if self._root is None:return
            for directory,folders,files in os.walk(self._root,followlinks=False):
                folders[:]=[name for name in folders if not (Path(directory)/name).is_symlink()]
                for name in folders+files:
                    path=Path(directory)/name
                    if not path.is_symlink() and all(term in str(path.relative_to(self._root)).casefold() for term in terms):yield path
        try:entries=sorted(candidates(),key=lambda p:(not p.is_dir(),p.name.casefold(),str(p)))
        except OSError:self.Browser.blockSignals(blocked);return
        for entry in entries:
            if entry.suffix.casefold()==".meta":continue
            asset=QListWidgetItem(self._Icon(entry),self._DisplayName(entry));asset.setSizeHint(QSize(104,86));asset.setTextAlignment(Qt.AlignmentFlag.AlignHCenter|Qt.AlignmentFlag.AlignBottom);asset.setData(Qt.ItemDataRole.UserRole,entry);asset.setData(Qt.ItemDataRole.UserRole+1,entry.name);asset.setFlags(asset.flags()|Qt.ItemFlag.ItemIsEditable|Qt.ItemFlag.ItemIsDragEnabled);self.Browser.addItem(asset)
            asset.setToolTip(str(entry.relative_to(self._root)) if self._root else str(entry))
            if str(entry) in selected:asset.setSelected(True)
        self.Browser.blockSignals(blocked)
    @staticmethod
    def _DisplayName(path)->str:
        path=Path(path);return path.name if path.is_dir() else path.stem
    def _TreeSelected(self,item,_column)->None:
        if item:self.AssetSelected.emit(Path(item.data(0,Qt.ItemDataRole.UserRole)))
    def _SelectionChanged(self)->None:
        items=self.Browser.selectedItems()
        if len(items)==1:self.AssetSelected.emit(items[0].data(Qt.ItemDataRole.UserRole))
        elif not items:self.SelectionCleared.emit()
    def _Activate(self,item)->None:
        path=Path(item.data(Qt.ItemDataRole.UserRole))
        if path.is_dir():
            self.Search.clear()
            for match in self.Tree.findItems(path.name,Qt.MatchFlag.MatchExactly|Qt.MatchFlag.MatchRecursive):
                if Path(match.data(0,Qt.ItemDataRole.UserRole))==path:self.Tree.setCurrentItem(match);return
        if self.ExternalOpener and self.ExternalOpener.Supports(path):self._OpenExternal([path]);return
        if path.suffix.lower()==".bscene" and callable(self.SceneLoadedChecker) and self.SceneLoadedChecker(path):return
        self.AssetActivated.emit(path)
    def _Unique(self,name):
        candidate=Path(self._folder or self._root)/name;stem,suffix=candidate.stem,candidate.suffix;number=1
        while candidate.exists():candidate=candidate.with_name(f"{stem} {number}{suffix}");number+=1
        return candidate
    def _Create(self,kind)->None:
        names={"folder":"New Folder","scene":"New Scene.bscene","cpp":"NewComponent.cpp","shader":"NewShader.shad","filament_shader":"NewShader.mat","material":"NewMaterial.matinst"};path=self._Unique(names[kind])
        if kind=="folder":path.mkdir()
        else:path.write_text(f'FormatVersion: 1\nSceneUUID: "{uuid.uuid4()}"\nEntities:\n' if kind=="scene" else '#include <Bazzalt/Script.h>\n\nCOMPONENT(NewComponent) {\npublic:\n    PROPERTY(float, Speed, 1.0f)\n\n    void OnUpdate(float deltaTime) override { (void)deltaTime; }\n};\n' if kind=="cpp" else "shader NewShader {\n    properties { roughness: float = 0.5; }\n    material { color = vec4(1.0); roughness = roughness; }\n}\n" if kind=="shader" else 'material { name: "NewShader", shadingModel: lit }\nfragment { void material(inout MaterialInputs material) { prepareMaterial(material); material.baseColor = vec4(1.0); } }\n' if kind=="filament_shader" else '{\n  "version": 1,\n  "shader": null,\n  "properties": {}\n}\n',encoding="utf-8")
        self.Refresh(path);self.Browser.editItem(self.Browser.currentItem())
    def _Import(self)->None:
        if self._folder is None:return
        dialog=QFileDialog(self,self._localization.Translate("assets.import"));dialog.setOption(QFileDialog.Option.DontUseNativeDialog);dialog.setFileMode(QFileDialog.FileMode.ExistingFiles)
        if not dialog.exec():return
        created=None
        for value in dialog.selectedFiles():
            try:created=self._Unique(Path(value).name);shutil.copy2(Path(value),created)
            except OSError:pass
        self.Refresh(created)
    def _ItemRenamed(self,item)->None:
        old=Path(item.data(Qt.ItemDataRole.UserRole));name=item.text().strip()
        if not name or name==item.data(Qt.ItemDataRole.UserRole+1):
            self.Browser.blockSignals(True);item.setText(self._DisplayName(old));self.Browser.blockSignals(False);return
        target=old.with_name(name)
        if not self._InsideRoot(target) or target.exists():
            self.Browser.blockSignals(True);item.setText(self._DisplayName(old));self.Browser.blockSignals(False);return
        if old.suffix.lower()==".bscene" and callable(self.SceneRenameHandler):
            if self.SceneRenameHandler(old,target):return
            self.Browser.blockSignals(True);item.setText(self._DisplayName(old));self.Browser.blockSignals(False);return
        try:old.rename(target)
        except OSError:
            self.Browser.blockSignals(True);item.setText(self._DisplayName(old));self.Browser.blockSignals(False);return
        meta=old.with_name(old.name+".meta")
        if meta.exists():
            try:meta.rename(target.with_name(target.name+".meta"))
            except OSError:pass
        self.Refresh(target)
    def _Delete(self)->None:
        for item in self.Browser.selectedItems():
            path=Path(item.data(Qt.ItemDataRole.UserRole))
            if self._InsideRoot(path):
                if not QFile.moveToTrash(str(path)):self.AssetOperationFailed.emit(self._localization.Translate("assets.file_busy",name=path.name));continue
                meta=path.with_name(path.name+".meta");QFile.moveToTrash(str(meta)) if meta.exists() else None
        self.Refresh();self.SelectionCleared.emit()
    def _Copy(self)->None:self._clipboard=[Path(i.data(Qt.ItemDataRole.UserRole)) for i in self.Browser.selectedItems()]
    def _Paste(self)->None:
        if self._folder is None:return
        created=None
        for source in self._clipboard:
            if not source.exists() or not self._InsideRoot(source):continue
            try:created=self._Unique(source.name);shutil.copytree(source,created) if source.is_dir() else shutil.copy2(source,created)
            except OSError:pass
        self.Refresh(created)
    def _ShowContextMenu(self,widget,position)->None:
        target=widget.itemAt(position)
        if target is not None and widget is self.Browser and not target.isSelected():self.Browser.setCurrentItem(target)
        reveal_paths=[Path(target.data(0,Qt.ItemDataRole.UserRole))] if widget is self.Tree and target is not None else [Path(item.data(Qt.ItemDataRole.UserRole)) for item in self.Browser.selectedItems()]
        tr=self._localization.Translate;menu=QMenu(self);create=menu.addMenu(tr("assets.create"))
        for key,label in (("folder","assets.folder"),("scene","assets.scene"),("cpp","assets.native_cpp"),("shader","assets.shader"),("filament_shader","materials.filament_shader"),("material","assets.material")):
            action=create.addAction(tr(label));action.triggered.connect(lambda _=False,k=key:self._Create(k))
        menu.addAction(tr("assets.import"),self._Import);menu.addSeparator();selected=bool(self.Browser.selectedItems()) if widget is self.Browser else False
        current=Path(self.Browser.currentItem().data(Qt.ItemDataRole.UserRole)) if widget is self.Browser and self.Browser.currentItem() else None
        external=bool(self.ExternalOpener and reveal_paths and all(path.is_file() and self.ExternalOpener.Supports(path) for path in reveal_paths))
        open_action=menu.addAction(tr("assets.open"));open_action.setVisible(external);open_action.triggered.connect(lambda _=False:self._OpenExternal(reveal_paths))
        open_with=menu.addAction(tr("assets.open_with"));open_with.setVisible(external);open_with.triggered.connect(lambda _=False:self._OpenExternal(reveal_paths,True))
        load=menu.addAction(tr("assets.load_scene"));is_scene=bool(current and current.suffix.lower()==".bscene");load.setVisible(is_scene);load.setEnabled(bool(is_scene and not (callable(self.SceneLoadedChecker) and self.SceneLoadedChecker(current))));load.triggered.connect(lambda:self.LoadSceneRequested.emit(current) if current else None)
        rename=menu.addAction(tr("assets.rename"));rename.setEnabled(len(self.Browser.selectedItems())==1);rename.triggered.connect(lambda:self.Browser.editItem(self.Browser.currentItem()));copy=menu.addAction(tr("assets.copy"));copy.setEnabled(selected);copy.triggered.connect(self._Copy);paste=menu.addAction(tr("assets.paste"));paste.setEnabled(bool(self._clipboard));paste.triggered.connect(self._Paste);delete=menu.addAction(tr("assets.delete"));delete.setEnabled(selected);delete.triggered.connect(self._Delete);menu.addSeparator();menu.addAction(tr("assets.refresh"),self.Refresh)
        reveal_key="assets.reveal" if sys.platform=="win32" else "assets.reveal_finder" if sys.platform=="darwin" else "assets.reveal_manager"
        reveal=menu.addAction(tr(reveal_key));reveal.setEnabled(bool(reveal_paths));reveal.triggered.connect(lambda _=False:self._Reveal(reveal_paths))
        self.ContextMenuRequested.emit(menu,widget.mapToGlobal(position));menu.exec(widget.mapToGlobal(position))

    def _Reveal(self,paths)->None:
        try:RevealFiles([path for path in paths if self._InsideRoot(path)])
        except OSError as error:self.AssetOperationFailed.emit(self._localization.Translate("assets.reveal_failed",error=str(error)))

    def _OpenExternal(self,paths,choose=False)->None:
        if not self.ExternalOpener:return
        try:
            if any(not self._InsideRoot(path) for path in paths):raise ValueError("Asset is outside the project")
            self.ExternalOpener.Open(paths,self.window(),self._localization,choose)
        except (OSError,ValueError) as error:
            self.AssetOperationFailed.emit(self._localization.Translate("assets.open_failed",error=str(error)))

__all__=["AssetBrowserPanel","IMAGE_EXTENSIONS","MODEL_EXTENSIONS","SHADER_EXTENSIONS","ENVIRONMENT_EXTENSIONS"]
