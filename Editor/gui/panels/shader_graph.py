"""Dockable, themed shader authoring. Graph history never mutates the game scene."""
from __future__ import annotations
import copy,json,math,re
from pathlib import Path
from PySide6.QtCore import QPointF,QRectF,Qt,Signal,QTimer,QMimeData,QFileSystemWatcher,QSize
from PySide6.QtGui import QColor,QPainter,QPainterPath,QPen,QDrag,QUndoCommand,QUndoStack,QPixmap,QIcon
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QSplitter,QLineEdit,QTreeWidget,QTreeWidgetItem,QGraphicsView,QGraphicsScene,QGraphicsObject,QGraphicsPathItem,QLabel,QToolButton,QFormLayout,QComboBox,QMenu,QScrollArea,QApplication,QStyle,QSizePolicy
from ..widgets.fields import AssetPickerInput,FloatInput,IntInput,Vec2Input,Vec3Input,Vec4Input,BoolInput,EnumInput
from ... import shader_graph as Graph

class PreviewImage(QLabel):
    """Keep the previous result visible while the replacement is compiling."""
    def __init__(self,panel):
        super().__init__(panel.Tr('graph.preview'));self.Panel=panel;self.Busy=False;self.Angle=0
        self.setMinimumSize(120,120);self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.Spinner=QTimer(self);self.Spinner.setInterval(40);self.Spinner.timeout.connect(self.Advance)
    def Advance(self):self.Angle=(self.Angle+12)%360;self.update()
    def SetBusy(self,value):
        self.Busy=value
        if value:self.Spinner.start()
        else:self.Spinner.stop()
        self.update()
    def paintEvent(self,event):
        super().paintEvent(event)
        if not self.Busy:return
        theme=self.Panel.Themes.GetTheme();p=QPainter(self);p.setRenderHint(QPainter.RenderHint.Antialiasing)
        shade=QColor(theme.background);shade.setAlpha(185);p.fillRect(self.rect(),shade)
        center=self.rect().center();ring=QRectF(center.x()-14,center.y()-24,28,28)
        p.setPen(QPen(QColor(theme.border),3));p.drawEllipse(ring)
        p.setPen(QPen(QColor(theme.accent),3,Qt.PenStyle.SolidLine,Qt.PenCapStyle.RoundCap));p.drawArc(ring,-self.Angle*16,100*16)
        p.setPen(QColor(theme.text_muted));p.drawText(QRectF(0,center.y()+13,self.width(),30),Qt.AlignmentFlag.AlignCenter,self.Panel.Tr('graph.loading'))

class Palette(QTreeWidget):
    def startDrag(self,actions):
        item=self.currentItem();kind=item.data(0,Qt.ItemDataRole.UserRole) if item else None
        if not kind:return
        mime=QMimeData();mime.setData('application/x-bazzalt-shader-node',kind.encode());drag=QDrag(self);drag.setMimeData(mime);drag.exec(Qt.DropAction.CopyAction)

class Node(QGraphicsObject):
    def __init__(self,panel,data):
        super().__init__();self.Panel=panel;self.Data=data;self.Spec=Graph.NODES[data['type']]
        self.setFlags(self.GraphicsItemFlag.ItemIsMovable|self.GraphicsItemFlag.ItemIsSelectable|self.GraphicsItemFlag.ItemSendsGeometryChanges)
        self.setPos(*data.get('position',[0,0]));self.setZValue(2)
    def boundingRect(self):return QRectF(-7,-3,194,46+max(1,len(self.Spec.Inputs))*24)
    def Socket(self,name=None):return self.mapToScene(QPointF(180,20) if name is None else QPointF(0,49+self.Spec.Inputs.index(name)*24))
    def Hit(self,point):
        if self.Spec.Output not in ('surface','vertex') and (point-self.Socket()).manhattanLength()<12:return ('out',self.Data['id'],None)
        for name in self.Spec.Inputs:
            if (point-self.Socket(name)).manhattanLength()<12:return ('in',self.Data['id'],name)
    def paint(self,painter,option,widget=None):
        t=self.Panel.Themes.GetTheme();rect=self.boundingRect().adjusted(7,3,-7,-3)
        painter.setPen(QPen(QColor(t.accent if self.isSelected() else t.border),2 if self.isSelected() else 1));painter.setBrush(QColor(t.surface));painter.drawRoundedRect(rect,4,4)
        title=self.Panel.Tr('graph.node.'+self.Data['type'])
        if self.Data['type'] in ('Property','Texture'):title+=': '+self.Data.get('values',{}).get('name','GraphValue')
        painter.setPen(QColor(t.text));painter.drawText(QRectF(10,3,159,30),Qt.AlignmentFlag.AlignVCenter,painter.fontMetrics().elidedText(title,Qt.TextElideMode.ElideRight,155))
        for i,name in enumerate(self.Spec.Inputs):
            y=49+i*24;painter.setBrush(QColor(t.accent));painter.drawEllipse(QPointF(0,y),5,5);painter.setPen(QColor(t.text_muted));painter.drawText(QRectF(12,y-10,155,20),Qt.AlignmentFlag.AlignVCenter,self.Panel.Tr('graph.socket.'+name))
        if self.Spec.Output not in ('surface','vertex'):painter.setBrush(QColor(t.accent));painter.drawEllipse(QPointF(180,20),5,5)
    def itemChange(self,change,value):
        if change==self.GraphicsItemChange.ItemPositionHasChanged and self.scene():self.Data['position']=[self.pos().x(),self.pos().y()];self.Panel.DrawEdges()
        return super().itemChange(change,value)

class Canvas(QGraphicsView):
    def __init__(self,panel):
        super().__init__(panel);self.Panel=panel;self.setScene(QGraphicsScene(self));self.setRenderHint(QPainter.RenderHint.Antialiasing);self.setDragMode(self.DragMode.RubberBandDrag);self.setAcceptDrops(True);self.Start=None;self.Before=None;self.Temp=None
        self.setSceneRect(-8000,-8000,16000,16000);self.setTransformationAnchor(self.ViewportAnchor.AnchorUnderMouse)
    def wheelEvent(self,event):
        factor=1.15 if event.angleDelta().y()>0 else 1/1.15
        if .2<self.transform().m11()*factor<3:self.scale(factor,factor)
        event.accept()
    def drawBackground(self,painter,rect):
        t=self.Panel.Themes.GetTheme();painter.fillRect(rect,QColor(t.background));painter.setPen(QPen(QColor(t.surface_alt),0))
        for x in range(math.floor(rect.left()/32)*32,math.ceil(rect.right()/32)*32,32):painter.drawLine(QPointF(x,rect.top()),QPointF(x,rect.bottom()))
        for y in range(math.floor(rect.top()/32)*32,math.ceil(rect.bottom()/32)*32,32):painter.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y))
    def dragEnterEvent(self,event):event.acceptProposedAction() if event.mimeData().hasFormat('application/x-bazzalt-shader-node') or self.Asset(event.mimeData()) else event.ignore()
    def dragMoveEvent(self,event):self.dragEnterEvent(event)
    def Asset(self,mime):
        if not mime.hasFormat('application/x-bazzalt-asset') or not self.Panel.Runtime:return None
        try:
            raw=bytes(mime.data('application/x-bazzalt-asset'))
            if len(raw)>32768:return None
            record=self.Panel.Runtime.ResolveAssetRecord(raw.decode('utf-8'))
            return record if record and record.get('extension','').lower() in {'.png','.jpg','.jpeg','.bmp','.tga','.webp','.btexture'} else None
        except (ValueError,UnicodeError,RuntimeError):return None
    def dropEvent(self,event):
        point=self.mapToScene(event.position().toPoint());asset=self.Asset(event.mimeData())
        if asset:
            def Op():
                node=Graph.AddNode(self.Panel.Graph,'Texture',(point.x(),point.y()));node['values']={'name':('Tex_'+re.sub('[^A-Za-z0-9_]','_',Path(asset.get('path','Texture')).stem))[:48],'texture':asset['uuid']}
                sample=Graph.AddNode(self.Panel.Graph,'SampleTexture',(point.x()+230,point.y()));sample['inputs']['texture']=node['id']
            self.Panel.Edit(Op);event.acceptProposedAction();return
        try:
            raw=bytes(event.mimeData().data('application/x-bazzalt-shader-node'))
            if len(raw)>64:return
            kind=raw.decode('utf-8')
        except UnicodeError:event.ignore();return
        if kind in Graph.NODES:self.Panel.Add(kind,point);event.acceptProposedAction()
    def _Socket(self,point):
        for node in self.Panel.Nodes.values():
            hit=node.Hit(point)
            if hit:return hit
    def mousePressEvent(self,event):
        point=self.mapToScene(event.position().toPoint())
        if event.button()==Qt.MouseButton.MiddleButton:self.setDragMode(self.DragMode.ScrollHandDrag);self._pan=event.position();event.accept();return
        if event.button()==Qt.MouseButton.LeftButton and self.Panel.Path:
            self.Start=self._Socket(point)
            if self.Start:
                self.Temp=self.scene().addPath(QPainterPath(),QPen(QColor(self.Panel.Themes.GetTheme().accent),2));event.accept();return
            self.Before=copy.deepcopy(self.Panel.Graph)
        super().mousePressEvent(event)
    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.MiddleButton:
            delta=event.position()-self._pan;self._pan=event.position();self.horizontalScrollBar().setValue(self.horizontalScrollBar().value()-int(delta.x()));self.verticalScrollBar().setValue(self.verticalScrollBar().value()-int(delta.y()));event.accept();return
        if self.Start:
            source=self.Panel.Nodes[self.Start[1]].Socket(self.Start[2]);self.Temp.setPath(self.Panel.Curve(source,self.mapToScene(event.position().toPoint())));event.accept();return
        super().mouseMoveEvent(event)
    def mouseReleaseEvent(self,event):
        if event.button()==Qt.MouseButton.MiddleButton:self.setDragMode(self.DragMode.RubberBandDrag);event.accept();return
        if self.Start:
            end=self._Socket(self.mapToScene(event.position().toPoint()));start=self.Start;self.Start=None
            if self.Temp:self.scene().removeItem(self.Temp);self.Temp=None
            if end and start[0]!=end[0]:
                source,dest=(start,end) if start[0]=='out' else (end,start);self.Panel.Connect(source[1],dest[1],dest[2])
            event.accept();return
        super().mouseReleaseEvent(event)
        if self.Before is not None:
            before=self.Before;self.Before=None
            if before!=self.Panel.Graph:self.Panel.Record(before)
    def contextMenuEvent(self,event):
        menu=QMenu(self);item=self.itemAt(event.pos())
        if isinstance(item,Node) and not item.isSelected():self.Panel.SelectNodes({item.Data['id']})
        if isinstance(item,QGraphicsPathItem) and item.data(0):menu.addAction(self.Panel.Tr('graph.disconnect'),lambda:self.Panel.Disconnect(*item.data(0)))
        else:
            menu.addAction(self.Panel.Tr('action.copy'),self.Panel.Copy);menu.addAction(self.Panel.Tr('action.paste'),self.Panel.Paste)
            menu.addAction(self.Panel.Tr('assets.delete'),self.Panel.Delete);menu.addAction(self.Panel.Tr('graph.frame'),self.Panel.Frame)
            if self.Panel.Path:menu.addAction(self.Panel.Tr('graph.unlock'),self.Panel.Unlock)
        menu.exec(event.globalPos())

class Change(QUndoCommand):
    def __init__(self,panel,before,after):super().__init__(panel.Tr('graph.edit'));self.Panel=panel;self.Before=before;self.After=after;self.First=True
    def redo(self):self.Panel.Apply(copy.deepcopy(self.After),self.First,self.Before);self.First=False
    def undo(self):self.Panel.Apply(copy.deepcopy(self.Before),False,self.After)

class Conversion(QUndoCommand):
    def __init__(self,panel,path):
        super().__init__(panel.Tr('graph.unlock'));self.Panel=panel;self.Path=path;self.Source=path.read_text(encoding='utf-8');self.Target=path.with_suffix('.shad');self.Error=None
    def redo(self):
        try:
            Graph.Unlock(self.Path);self.Panel.Path=None;self.Panel.Graph=Graph.NewGraph();self.Panel.SavedGraph=copy.deepcopy(self.Panel.Graph);self.Panel.Stamp=None;self.Panel.Watch();self.Panel.Rebuild();self.Panel.AssetChanged.emit(self.Target)
        except (OSError,ValueError) as e:self.Error=e;self.setObsolete(True);self.Panel.Error.setText(str(e))
    def undo(self):
        try:
            if self.Path.exists() or Path(str(self.Path)+'.meta').exists():raise ValueError('Graph destination already exists')
            # Refuse to discard external edits made since conversion.
            if self.Target.read_text(encoding='utf-8')!=Graph.Generate(json.loads(self.Source)):raise ValueError('Converted shader was modified externally')
            Graph._Write(self.Path,self.Source);old=Path(str(self.Target)+'.meta');new=Path(str(self.Path)+'.meta')
            try:
                if old.exists():old.rename(new)
            except OSError:self.Path.unlink(missing_ok=True);raise
            try:self.Target.unlink()
            except OSError:
                if new.exists():new.rename(old)
                self.Path.unlink(missing_ok=True);raise
            self.Panel.Path=self.Path;self.Panel.Graph=json.loads(self.Source);self.Panel.SavedGraph=copy.deepcopy(self.Panel.Graph);self.Panel.Stamp=self.Panel.FileStamp();self.Panel.Watch();self.Panel.Rebuild();self.Panel.AssetChanged.emit(self.Path)
            if self.Panel.isVisible():self.Panel.PreviewTimer.start()
        except (OSError,ValueError) as e:self.setObsolete(True);self.Panel.Error.setText(str(e))

class ShaderGraphPanel(QWidget):
    AssetChanged=Signal(object)
    def __init__(self,runtime,locale,resources,themes,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Locale=locale;self.Themes=themes;self.Resources=resources;self.Path=None;self.Graph=Graph.NewGraph();self.SavedGraph=copy.deepcopy(self.Graph);self.Stamp=None;self.Nodes={};self.Edges=[];self.History=QUndoStack(self);self.History.setUndoLimit(128);self.AssetProvider=lambda:[];self._clipboard=None
        # Watch the directory, not the file: Windows file handles must never
        # prevent an external editor (or our atomic writer) replacing the asset.
        self.Watcher=QFileSystemWatcher(self);self.Watcher.directoryChanged.connect(self.ExternalChanged)
        root=QVBoxLayout(self);root.setContentsMargins(4,4,4,4);bar=QHBoxLayout();self.Title=QLabel();self.Title.setTextFormat(Qt.TextFormat.PlainText);self.Title.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Preferred);self.Title.setMinimumWidth(0);bar.addWidget(self.Title,1);self.Buttons=[]
        def Button(key,callback,check=False):
            button=QToolButton();button.setProperty('locale_key',key);button.setText(self.Tr(key));button.setToolTip(self.Tr(key));button.setAccessibleName(self.Tr(key));button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly);button.setIconSize(QSize(16,16));button.setCheckable(check);button.clicked.connect(callback);bar.addWidget(button);self.Buttons.append(button);return button
        self.Pin=Button('graph.pin',lambda:None,True);undo=Button('action.undo',self.History.undo);redo=Button('action.redo',self.History.redo);Button('graph.frame',self.Frame);Button('graph.preview',self.Preview);Button('assets.refresh',self.Reload);Button('graph.unlock',self.Unlock);root.addLayout(bar)
        undo.setEnabled(False);redo.setEnabled(False);self.History.canUndoChanged.connect(undo.setEnabled);self.History.canRedoChanged.connect(redo.setEnabled)
        split=QSplitter();root.addWidget(split,1);sidebar=QWidget();left=QVBoxLayout(sidebar);left.setContentsMargins(0,0,0,0);self.Search=QLineEdit();self.Search.setPlaceholderText(self.Tr('graph.search'));left.addWidget(self.Search);self.Palette=Palette();self.Palette.setHeaderHidden(True);self.Palette.setDragEnabled(True);left.addWidget(self.Palette);split.addWidget(sidebar)
        categories={}
        for name,spec in Graph.NODES.items():
            if spec.Output=='surface':continue
            if spec.Category not in categories:
                categories[spec.Category]=QTreeWidgetItem(self.Palette,[self.Tr('graph.category.'+spec.Category)]);categories[spec.Category].setData(0,Qt.ItemDataRole.UserRole+1,spec.Category)
            item=QTreeWidgetItem(categories[spec.Category],[self.Tr('graph.node.'+name)]);item.setData(0,Qt.ItemDataRole.UserRole,name)
        self.Palette.expandAll();self.Search.textChanged.connect(self.Filter);self.Palette.itemDoubleClicked.connect(lambda item,_:self.Add(item.data(0,Qt.ItemDataRole.UserRole),self.Canvas.mapToScene(self.Canvas.viewport().rect().center())) if item.data(0,Qt.ItemDataRole.UserRole) else None)
        self.Canvas=Canvas(self);split.addWidget(self.Canvas);self.Canvas.scene().selectionChanged.connect(self.Inspect)
        right=QWidget();layout=QVBoxLayout(right);layout.setContentsMargins(0,0,0,0)
        self.Shading=QComboBox()
        for value in ('lit','unlit'):self.Shading.addItem(self.Tr('view.shading.'+value),value)
        self.Shading.currentIndexChanged.connect(lambda _:self.Option('shading',self.Shading.currentData()));layout.addWidget(self.Shading)
        self.Blending=QComboBox()
        for value in ('opaque','transparent','masked','add'):self.Blending.addItem(self.Tr('graph.blending.'+value),value)
        self.Blending.currentIndexChanged.connect(lambda _:self.Option('blending',self.Blending.currentData()));layout.addWidget(self.Blending)
        self.DoubleSided=QToolButton();self.DoubleSided.setText(self.Tr('graph.double_sided'));self.DoubleSided.setCheckable(True);self.DoubleSided.toggled.connect(lambda value:self.Option('doubleSided',value));layout.addWidget(self.DoubleSided)
        self.Mesh=AssetPickerInput(self.Tr('graph.sphere'),accepted_extensions={'.obj','.glb','.gltf'});self.Mesh.AssetResolver=getattr(runtime,'ResolveAssetRecord',None)
        self.Mesh.PickRequested.connect(self.PickMesh);self.Mesh.ValueChanged.connect(lambda _:self.Preview());layout.addWidget(self.Mesh)
        self.Image=PreviewImage(self);layout.addWidget(self.Image)
        scroll=QScrollArea();scroll.setWidgetResizable(True);self.Fields=QWidget();self.Form=QFormLayout(self.Fields);scroll.setWidget(self.Fields);layout.addWidget(scroll,1);split.addWidget(right);split.setSizes([170,650,250])
        self.Error=QLabel();self.Error.setTextFormat(Qt.TextFormat.PlainText);self.Error.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse);self.Error.setMaximumHeight(120);self.Error.setWordWrap(True);root.addWidget(self.Error);themes.ThemeChanged.connect(lambda _:self.Repaint());self.Rebuild()
        self.PreviewTimer=QTimer(self);self.PreviewTimer.setSingleShot(True);self.PreviewTimer.setInterval(350);self.PreviewTimer.timeout.connect(self.Preview)
        locale.LocaleChanged.connect(self.Retranslate);self.Repaint()
    def Tr(self,key):return self.Locale.Translate(key)
    def Retranslate(self,*_):
        for button in self.findChildren(QToolButton):
            if button.property('locale_key'):
                text=self.Tr(button.property('locale_key'));button.setText(text);button.setToolTip(text);button.setAccessibleName(text)
        self.DoubleSided.setText(self.Tr('graph.double_sided'));self.Search.setPlaceholderText(self.Tr('graph.search'))
        for i in range(self.Palette.topLevelItemCount()):
            category=self.Palette.topLevelItem(i);category.setText(0,self.Tr('graph.category.'+category.data(0,Qt.ItemDataRole.UserRole+1)))
            for j in range(category.childCount()):item=category.child(j);item.setText(0,self.Tr('graph.node.'+item.data(0,Qt.ItemDataRole.UserRole)))
        for combo,prefix in ((self.Shading,'view.shading.'),(self.Blending,'graph.blending.')):
            for i in range(combo.count()):combo.setItemText(i,self.Tr(prefix+combo.itemData(i)))
        self.Rebuild();self.Repaint()
    def PickMesh(self):self.Mesh.ConfigureAssets(list(self.AssetProvider()));self.Mesh.OpenProjectPicker()
    def Filter(self,text):
        for i in range(self.Palette.topLevelItemCount()):
            category=self.Palette.topLevelItem(i)
            for j in range(category.childCount()):category.child(j).setHidden(text.casefold() not in category.child(j).text(0).casefold())
            category.setHidden(all(category.child(j).isHidden() for j in range(category.childCount())))
    def Open(self,path,force=False):
        if self.Pin.isChecked() and self.Path and not force:return False
        if not self.Flush():return False
        try:graph=Graph.Load(path)
        except (OSError,ValueError) as e:self.Error.setText(str(e));return False
        self.Path=Path(path);self.Graph=graph;self.SavedGraph=copy.deepcopy(graph);self.Stamp=self.FileStamp();self.Watch();self.History.clear();self.Error.clear();self.Rebuild();self.Frame();self.Preview();return True
    def FileStamp(self):
        try:stat=self.Path.stat();return stat.st_mtime_ns,stat.st_size
        except (OSError,AttributeError):return None
    def Watch(self):
        files=self.Watcher.directories()
        if self.Path and files==[str(self.Path.parent)]:return
        if files:self.Watcher.removePaths(files)
        if self.Path and self.Path.parent.exists():self.Watcher.addPath(str(self.Path.parent))
    def ExternalChanged(self,*_):
        if self.FileStamp()!=self.Stamp:
            focus=QApplication.focusWidget();editing=bool(focus and (focus is self.Fields or self.Fields.isAncestorOf(focus)))
            if self.Graph==self.SavedGraph and not editing:self.Reload()
            else:self.Error.setText(self.Tr('graph.external_change'))
        self.Watch()
    def Reload(self):
        if not self.Path:return
        try:
            graph=Graph.Load(self.Path);self.Graph=graph;self.SavedGraph=copy.deepcopy(graph);self.Stamp=self.FileStamp();self.History.clear();self.Rebuild();self.Error.clear();self.Watch();self.Preview()
        except (OSError,ValueError) as e:self.Error.setText(str(e))
    def Apply(self,graph,preserve_fields=False,previous=None):
        same=len(graph['nodes'])==len(self.Nodes) and all(n['id'] in self.Nodes and self.Nodes[n['id']].Spec is Graph.NODES[n['type']] for n in graph['nodes'])
        self.Graph=graph
        if same:
            for data in graph['nodes']:
                node=self.Nodes[data['id']];node.Data=data;node.setPos(*data.get('position',[0,0]));node.update()
            self.DrawEdges();self.SyncOptions()
            connections=not previous or any(a.get('inputs')!=b.get('inputs') for a,b in zip(previous['nodes'],graph['nodes']))
            kinds=previous and any(a.get('values',{}).get('kind')!=b.get('values',{}).get('kind') for a,b in zip(previous['nodes'],graph['nodes']))
            if not preserve_fields or connections or kinds:self.Inspect()
        else:self.Rebuild()
        self.Flush()
        if self.isVisible():self.PreviewTimer.start()
    def showEvent(self,event):
        super().showEvent(event);QTimer.singleShot(0,self.Frame)
    def Record(self,before):
        after=copy.deepcopy(self.Graph)
        if before!=after:self.History.push(Change(self,before,after))
    def Edit(self,operation):
        if not self.Path:return
        before=copy.deepcopy(self.Graph)
        try:operation();Graph.Validate(self.Graph);self.Record(before);self.Error.clear()
        except (OSError,ValueError) as e:self.Graph=before;self.Rebuild();self.Error.setText(str(e))
    def SelectNodes(self,identities):
        self.Canvas.scene().blockSignals(True)
        for identity,node in self.Nodes.items():node.setSelected(identity in identities)
        self.Canvas.scene().blockSignals(False);self.Canvas.scene().selectionChanged.emit()
    def Add(self,kind,point):
        created=[]
        self.Edit(lambda:created.append(Graph.AddNode(self.Graph,kind,(point.x(),point.y()))['id']))
        if created:self.SelectNodes(set(created))
    def Connect(self,source,target,socket):
        def Op():
            next(n for n in self.Graph['nodes'] if n['id']==target)['inputs'][socket]=source;Graph.Generate(self.Graph)
        self.Edit(Op)
    def Disconnect(self,target,socket):self.Edit(lambda:next(n for n in self.Graph['nodes'] if n['id']==target)['inputs'].pop(socket,None))
    def Delete(self):
        selected={n.Data['id'] for n in self.Canvas.scene().selectedItems() if isinstance(n,Node) and n.Data['type']!='Material'}
        def Op():
            self.Graph['nodes']=[n for n in self.Graph['nodes'] if n['id'] not in selected]
            for node in self.Graph['nodes']:node['inputs']={k:v for k,v in node['inputs'].items() if v not in selected}
        if selected:self.Edit(Op)
    def Copy(self):self._clipboard=copy.deepcopy([n.Data for n in self.Canvas.scene().selectedItems() if isinstance(n,Node) and n.Data['type']!='Material'])
    def Paste(self):
        if not self._clipboard:return
        import uuid
        created=[]
        def Op():
            copies=copy.deepcopy(self._clipboard);ids={n['id']:uuid.uuid4().hex for n in copies}
            for node in copies:node['id']=ids[node['id']];node['position']=[x+32 for x in node['position']];node['inputs']={k:ids[v] for k,v in node['inputs'].items() if v in ids}
            self.Graph['nodes']+=copies
            created.extend(n['id'] for n in copies)
        self.Edit(Op)
        if created:self.SelectNodes(set(created))
    def Duplicate(self):
        previous=self._clipboard;self.Copy();self.Paste();self._clipboard=previous
    def Option(self,key,value):
        if self.Graph.get(key)!=value:self.Edit(lambda:self.Graph.__setitem__(key,value))
    def Rebuild(self):
        selected={n.Data["id"] for n in self.Canvas.scene().selectedItems() if isinstance(n,Node)}
        self.Canvas.scene().blockSignals(True);self.Nodes={};self.Edges=[];self.Canvas.scene().clear()
        if self.Path:
            for data in self.Graph['nodes']:
                node=Node(self,data);self.Nodes[data['id']]=node;self.Canvas.scene().addItem(node);node.setSelected(data['id'] in selected)
        self.Canvas.scene().blockSignals(False);self.DrawEdges();self.Title.setText(self.Path.name if self.Path else self.Tr('graph.empty'))
        self.Title.setToolTip(str(self.Path) if self.Path else '')
        if not self.Path:
            self.Image.clear();self.Image.setText(self.Tr('graph.preview'))
            self.Image.SetBusy(False)
            if hasattr(self,'PreviewService'):self.PreviewService.Generation+=1;self.PreviewService.Pending=None
        self.SyncOptions();self.Inspect()
    def SyncOptions(self):
        for widget,value in [(self.Shading,self.Graph.get('shading','lit')),(self.Blending,self.Graph.get('blending','opaque'))]:
            old=widget.blockSignals(True);widget.setCurrentIndex(widget.findData(value));widget.blockSignals(old)
        old=self.DoubleSided.blockSignals(True);self.DoubleSided.setChecked(self.Graph.get('doubleSided',False));self.DoubleSided.blockSignals(old)
    @staticmethod
    def Curve(a,b):
        path=QPainterPath(a);distance=max(50,abs(a.x()-b.x())*.5);path.cubicTo(a+QPointF(distance,0),b-QPointF(distance,0),b);return path
    def DrawEdges(self):
        for edge in self.Edges:self.Canvas.scene().removeItem(edge)
        self.Edges=[]
        for node in self.Nodes.values():
            for socket,source in node.Data.get('inputs',{}).items():
                if source not in self.Nodes:continue
                edge=self.Canvas.scene().addPath(self.Curve(self.Nodes[source].Socket(),node.Socket(socket)),QPen(QColor(self.Themes.GetTheme().accent),2));edge.setZValue(0);edge.setData(0,(node.Data['id'],socket));self.Edges.append(edge)
    def Inspect(self):
        while self.Form.rowCount():
            row=self.Form.takeRow(0)
            for item in (row.labelItem,row.fieldItem):
                if item and item.widget():item.widget().hide();item.widget().deleteLater()
        selected=[n for n in self.Canvas.scene().selectedItems() if isinstance(n,Node)]
        if len(selected)!=1:return
        node=selected[0].Data;kind=node['type'];spec=Graph.NODES[kind]
        fields=list(spec.Inputs)
        if spec.Expression=='value':fields=['value']
        if kind in ('Property','Texture'):fields=['name']+(['kind','value'] if kind=='Property' else [])
        if kind=='Swizzle':fields=['mask']
        for field in fields:
            if field in node['inputs']:continue
            value=node['values'].get(field,'GraphValue' if field=='name' else 'float' if field=='kind' else 'x' if field=='mask' else False if field=='value' and kind=='Boolean' else 1 if field=='value' and kind=='Integer' else 1. if field=='value' and kind in ('Float','Property') else [1.]*Graph.KINDS.get(spec.Output,1) if field=='value' else Graph.DefaultInput(node,field))
            def Commit(value,f=field,identity=node['id']):
                if isinstance(value,tuple):value=list(value)
                def Op():
                    values=next(n for n in self.Graph['nodes'] if n['id']==identity)['values'];values[f]=value
                    if f=='kind':values['value']=False if value=='bool' else 1 if value=='int' else 1. if value=='float' else [1.]*Graph.KINDS[value]
                self.Edit(Op)
            if field=='kind':
                editor=EnumInput();editor.SetOptions([(key,key) for key in Graph.KINDS if key!='texture2d']);editor.SetValue(value);editor.currentIndexChanged.connect(lambda _,e=editor,c=Commit:c(e.GetValue()))
            elif isinstance(value,bool):editor=BoolInput(value);editor.ValueChanged.connect(Commit)
            elif isinstance(value,int):editor=IntInput(value=value);editor.valueChanged.connect(Commit)
            elif isinstance(value,float):editor=FloatInput(minimum=-1e30,maximum=1e30,value=value);editor.valueChanged.connect(Commit)
            elif isinstance(value,list) and len(value) in (2,3,4):editor={2:Vec2Input,3:Vec3Input,4:Vec4Input}[len(value)](value);editor.ValueChanged.connect(Commit)
            else:
                editor=QLineEdit(str(value));editor.editingFinished.connect(lambda e=editor,c=Commit:c(e.text()))
            self.Form.addRow(self.Tr('graph.socket.'+field),editor)
        if kind=='Texture':
            picker=AssetPickerInput(self.Tr('properties.select_project_asset'),accepted_extensions={'.png','.jpg','.jpeg','.bmp','.tga','.webp','.btexture'})
            picker.AssetResolver=getattr(self.Runtime,'ResolveAssetRecord',None);picker.ConfigureAssets(list(self.AssetProvider()));picker.SetValue(node['values'].get('texture'));picker.PickRequested.connect(picker.OpenProjectPicker)
            picker.ValueChanged.connect(lambda value,identity=node['id']:self.Edit(lambda:next(n for n in self.Graph['nodes'] if n['id']==identity)['values'].__setitem__('texture',value)))
            self.Form.addRow(self.Tr('graph.socket.texture'),picker)
    def Frame(self):
        if self.Nodes:self.Canvas.fitInView(self.Canvas.scene().itemsBoundingRect().adjusted(-60,-60,60,60),Qt.AspectRatioMode.KeepAspectRatio)
    def Repaint(self):
        self.Canvas.viewport().update()
        icons=['pin','undo','redo','frame','preview','refresh','unlock']
        for button,name in zip(self.Buttons,icons):
            theme='dark' if QColor(self.Themes.GetTheme().background).lightness()<128 else 'light'
            source=self.Resources.Icon('icons/shader_graph/'+name+'.svg').pixmap(16,16);icon=QIcon()
            for mode,color in ((QIcon.Mode.Normal,self.Themes.GetTheme().text),(QIcon.Mode.Disabled,self.Themes.GetTheme().text_disabled)):
                pixmap=QPixmap(source.size());pixmap.fill(Qt.GlobalColor.transparent);painter=QPainter(pixmap);painter.drawPixmap(0,0,source);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),QColor(color));painter.end();icon.addPixmap(pixmap,mode)
            button.setIcon(icon)
        for node in self.Nodes.values():node.update()
        self.DrawEdges()
    def Flush(self):
        if not self.Path:return True
        if self.Graph==self.SavedGraph:return True
        if self.FileStamp()!=self.Stamp:self.Error.setText(self.Tr('graph.external_change'));return False
        try:
            changed=Graph.Save(self.Path,self.Graph);self.SavedGraph=copy.deepcopy(self.Graph);self.Stamp=self.FileStamp();self.Watch()
            if changed:self.AssetChanged.emit(self.Path)
            return True
        except (OSError,ValueError) as e:self.Error.setText(str(e));return False
    def Unlock(self):
        if self.Path and self.Flush():self.History.push(Conversion(self,self.Path))
    def Preview(self):
        if not self.Path or not self.Flush():return
        try:
            if not hasattr(self,'PreviewService'):
                from ...shader_graph_preview import ShaderGraphPreview
                self.PreviewService=ShaderGraphPreview(self.Runtime,self);self.PreviewService.Ready.connect(self.ShowPreview);self.PreviewService.Failed.connect(self.PreviewFailed)
            self.Image.SetBusy(True);self.PreviewService.Request(self.Path,self.Mesh.GetValue())
        except (OSError,ValueError) as e:self.PreviewFailed(str(e))
    def PreviewFailed(self,message):self.Image.SetBusy(False);self.Error.setText(message)
    def ShowPreview(self,image):
        from PySide6.QtGui import QPixmap
        self.Image.SetBusy(False);self.Image.setPixmap(QPixmap.fromImage(image).scaled(240,240,Qt.AspectRatioMode.KeepAspectRatio,Qt.TransformationMode.SmoothTransformation));self.Error.clear()
