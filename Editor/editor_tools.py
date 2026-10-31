"""Editor-only Lua tools: scoped capabilities, scene transactions and owned widgets."""
from __future__ import annotations
import math,re
from pathlib import Path
from PySide6.QtCore import QObject,QEvent,Qt,QTimer
from PySide6.QtGui import QAction,QKeySequence
from PySide6.QtWidgets import QDialog,QVBoxLayout,QLabel,QPushButton,QLineEdit,QDoubleSpinBox,QCheckBox,QComboBox,QApplication

METHODS=('GetVersion','GetScene','GetScenes','GetEntities','GetEntity','GetSelection','SetSelection','CreateEntity','InstantiateModel','DeleteEntity','RenameEntity','SetTransform','Translate','SetParent','AddComponent','RemoveComponent','SetComponentProperty','GetAssets','ReadAssetText','WriteAssetText','CreateMeshAsset','SelectAsset','GetAssetFolder','RefreshAssets','GetPanels','OpenPanel','ClosePanel','GetSceneCamera','SetSceneCamera','ScreenRay','Raycast','DrawLines','ClearLines','BeginUndo','EndUndo','CancelUndo','Log','CreateDialog','AddWidget','GetValue','SetValue','ShowDialog','CloseDialog','SetSceneInput')
MUTATIONS={'InstantiateModel','WriteAssetText','CreateMeshAsset','CreateEntity','DeleteEntity','RenameEntity','SetTransform','Translate','SetParent','AddComponent','RemoveComponent','SetComponentProperty'}

def Vector(value,count):
    if not isinstance(value,(list,tuple)) or len(value)!=count:raise ValueError('Expected '+str(count)+' finite numbers')
    if any(type(v) not in (int,float) for v in value):raise ValueError("Expected numeric editor coordinates")
    result=[float(v) for v in value]
    if not all(math.isfinite(v) and abs(v)<=1e12 for v in result):raise ValueError('Editor vector is not finite or exceeds limits')
    return result

class EditorTools(QObject):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Sessions={};self.Actions={};self.Sources={};self.Labels={};self.Dialogs={};self.Guides={};self.Input=set();self.CallingOwner=None;self.Provenance=set();self.SceneExclusions=set();self.SelectionOrigins=set();self.LastSelection=();self.Owner=None;self.TargetScenes=set();self.Manual=False;self.Busy=False;self.Closed=False
        editor.Scene.Surface.installEventFilter(self)
        self.SceneTimer=QTimer(self);self.SceneTimer.setSingleShot(True);self.SceneTimer.timeout.connect(self.SceneChanged)
        self.SelectionTimer=QTimer(self);self.SelectionTimer.setInterval(50);self.SelectionTimer.timeout.connect(self.PollSelection);self.SelectionTimer.start()
        editor.Runtime.SceneChanged.connect(self.QueueSceneChanged)
        editor.Runtime.ProjectChanging.connect(self.PrepareProjectChange)
        editor.Runtime.ProjectChangeAborted.connect(self.Reset)
        editor.Runtime.ProjectChanged.connect(lambda *_:self.Reset())
        menu=editor.Scene.ControlsMenu
        for action in (editor.MenuBar.GizmosAction,editor.MenuBar.IconsAction,editor.MenuBar.GridAction,editor.MenuBar.StatsAction):menu.addAction(action)
        menu.addSeparator()
    def Error(self,error,owner='Editor Tools'):
        from .gui.panels import ConsoleLevel
        self.Editor.Console.AddMessage(str(error),ConsoleLevel.Error,True,self.Labels.get(owner,owner))
    def Load(self,owner,source,modules=None):
        from .runtime import _LoadNativeModule
        native=_LoadNativeModule()
        if native is None or not hasattr(native,'EditorLua'):raise RuntimeError('Editor Lua host is not built')
        return native.EditorLua(source,modules or {},lambda method,args:self.Command(owner,method,list(args)),list(METHODS))
    def Call(self,owner,function='OnRun',argument=None,source=None,modules=None):
        if self.Closed or self.Busy:return False
        if self.Editor.Runtime.IsPlaying():self.Error(self.Editor.Localization.Translate('extensions.stop_playing'),owner);return False
        self.Busy=True;self.CallingOwner=owner
        try:
            if owner not in self.Sessions:
                if source is None:return False
                if len(self.Sessions)>=16:raise ValueError("At most 16 editor Lua sessions may be active")
                self.Sessions[owner]=self.Load(owner,source,modules)
            result=self.Sessions[owner].call(function,argument)
            if self.Owner==owner and not self.Manual:self.End(owner)
            return result
        except Exception as error:
            if self.Owner==owner:self.End(owner,True)
            self.Error(error,owner);self.Drop(owner)
            action=self.Actions.get(owner)
            if action:
                blocked=action.blockSignals(True);action.setChecked(False);action.blockSignals(blocked);self.Editor._settings.setdefault('scene_tool_controls',{})[owner]=False
            return False
        finally:self.Busy=False;self.CallingOwner=None
    def Begin(self,owner,label='Editor tool',manual=False):
        c=self.Editor.Controller
        if self.Owner not in (None,owner) or (self.Owner is None and c.History._pending is not None):raise ValueError('Finish the current edit before running an editor tool')
        if self.Owner is None:c.History.Begin(label);self.Owner=owner;self.TargetScenes=set()
        if manual:self.Manual=True
    def End(self,owner,cancel=False):
        if self.Owner is None:return
        if self.Owner!=owner:raise ValueError('Another tool owns this transaction')
        c=self.Editor.Controller;before=c.History._pending[1] if c.History._pending else None
        try:
            if cancel:
                c.History.Cancel()
                if before is not None:self.Editor.Runtime.RestoreScene(before)
            elif c.History.Commit() and c.History.LastCommitSceneChanged:c.SetDirty(True,self.TargetScenes)
        except Exception:
            if not cancel:
                c.History.Cancel()
                if before is not None:self.Editor.Runtime.RestoreScene(before)
            raise
        finally:self.Owner=None;self.Manual=False
        c.RefreshHierarchy();c._RefreshInspectorValues();c._UpdateGizmo()
    def Drop(self,owner):
        if self.Owner==owner:self.End(owner,True)
        self.Sessions.pop(owner,None);self.Input.discard(owner);self.Guides.pop(owner,None)
        for key,dialog in list(self.Dialogs.items()):
            if key[0]==owner:dialog.close();dialog.deleteLater();del self.Dialogs[key]
        self.PublishGuides()
    def PrepareProjectChange(self):
        for owner in list(self.Sessions):self.Drop(owner)
    def Reset(self):
        for owner in list(self.Sessions):self.Drop(owner)
        if not self.Closed:
            for owner,action in self.Actions.items():
                if action.isChecked():
                    source,modules,function=self.Sources[owner]
                    self.Input.add(owner);QTimer.singleShot(0,self,lambda o=owner,s=source,m=modules,f=function:self.Call(o,f,{'enabled':True},s,m))
    def Shutdown(self):self.Closed=True;self.SceneTimer.stop();self.SelectionTimer.stop();self.Reset()
    def QueueSceneChanged(self):
        if self.Closed or not self.Input:return
        self.SceneExclusions.update(self.Provenance)
        if self.CallingOwner:self.SceneExclusions.add(self.CallingOwner)
        self.SceneTimer.start()
    def SceneChanged(self):
        excluded=self.SceneExclusions;self.SceneExclusions=set();prior=self.Provenance;self.Provenance=excluded
        try:self.Broadcast('OnSceneChanged',self.Editor.Runtime.SceneInfo(),excluded)
        finally:self.Provenance=prior
    def PollSelection(self):
        if self.Closed:return
        current=tuple(self.Editor.Controller.SelectedEntities);excluded=self.SelectionOrigins;self.SelectionOrigins=set()
        if current==self.LastSelection:return
        self.LastSelection=current;self.Broadcast('OnSelectionChanged',list(current),excluded)
    def Broadcast(self,event,payload,excluded=()):
        if self.Busy or self.Closed or self.Editor.Runtime.IsPlaying():return False
        handled=False
        for owner in list(self.Input):
            if owner not in excluded:handled=self.Call(owner,event,payload) or handled
        return handled
    def PublishGuides(self):self.Editor.Runtime.SetToolGuides([line for owner,lines in self.Guides.items() if owner not in self.Actions or not self.Actions[owner].isCheckable() or self.Actions[owner].isChecked() for line in lines][:2048])
    def eventFilter(self,watched,event):
        if watched is not self.Editor.Scene.Surface or not self.Input or self.Busy or self.Editor.Runtime.IsPlaying():return False
        kinds={QEvent.Type.MouseButtonPress:'OnScenePointerDown',QEvent.Type.MouseMove:'OnScenePointerMove',QEvent.Type.MouseButtonRelease:'OnScenePointerUp',QEvent.Type.KeyPress:'OnSceneKeyDown',QEvent.Type.KeyRelease:'OnSceneKeyUp'}
        name=kinds.get(event.type())
        if not name:return False
        payload={'modifiers':int(event.modifiers().value)}
        if hasattr(event,'position'):
            point=event.position();ray=watched._Ray(point.toPoint());payload.update(x=point.x(),y=point.y(),button=int(event.button().value),buttons=int(event.buttons().value),origin=[ray.Origin.X,ray.Origin.Y,ray.Origin.Z],direction=[ray.Direction.X,ray.Direction.Y,ray.Direction.Z])
        else:payload.update(key=event.key(),text=event.text(),repeat=event.isAutoRepeat())
        return self.Broadcast(name,payload)
    def AddControl(self,owner,title,source,modules=None,function='OnRun',shortcut='',checked=False,checkable=True):
        if owner in self.Actions:raise ValueError('Duplicate viewport tool')
        sequence=QKeySequence(shortcut) if shortcut else None
        if sequence is not None and (sequence.isEmpty() or any(a.shortcut()==sequence for a in self.Editor.findChildren(QAction))):raise ValueError('Shortcut is already assigned or invalid: '+shortcut)
        self.Sources[owner]=(source,modules,function);self.Labels[owner]=title
        saved=self.Editor._settings.get('scene_tool_controls',{})
        if isinstance(saved,dict) and checkable:checked=saved.get(owner,checked)
        action=QAction(title,self.Editor);action.setCheckable(checkable);action.setChecked(bool(checked))
        if sequence is not None:action.setShortcut(sequence);self.Editor.addAction(action)
        def toggle(value):
            self.Editor._settings.setdefault('scene_tool_controls',{})[owner]=value
            if self.Editor._settings_saver:self.Editor._settings_saver(self.Editor._settings)
            if value:self.Input.add(owner)
            else:self.Input.discard(owner);self.Guides.pop(owner,None);self.PublishGuides()
            self.Call(owner,function,{'enabled':value},source,modules)
        if checkable:action.toggled.connect(toggle)
        else:action.triggered.connect(lambda *_:self.Call(owner,function,{'enabled':True,'invoked':True},source,modules))
        self.Editor.Scene.ControlsMenu.addAction(action);self.Actions[owner]=action
        if checked and self.Editor.Runtime.ProjectInfo().get('project_path'):QTimer.singleShot(0,self,lambda:toggle(True))
        return action
    def Widget(self,owner,key):
        dialog_name,_,name=str(key).partition('/')
        root=self.Dialogs.get((owner,dialog_name));widget=root if not name else root.findChild(QObject,name) if root else None
        if widget is None:raise ValueError('Owned widget not found: '+str(key))
        return widget
    def Command(self,owner,method,args):
        e=self.Editor;r=e.Runtime;c=e.Controller
        def arg(i,default=None):return args[i] if len(args)>i else default
        if method in MUTATIONS:
            if r.IsPlaying():raise ValueError('Stop Play before editing project data')
            self.Begin(owner,self.Labels.get(owner,owner))
            scene=r.EntityDetails(str(arg(0))).get('scene_uuid') if method not in {'CreateEntity','InstantiateModel','WriteAssetText','CreateMeshAsset'} else r.SceneInfo().get('uuid')
            if scene:self.TargetScenes.add(str(scene))
        if method=='GetVersion':
            from Compiler.extensions import EngineVersion
            return {'api':1,'engine':EngineVersion()}
        if method=='GetScene':return r.SceneInfo()
        if method=='GetScenes':return r.LoadedScenes()
        if method=='GetEntities':return r.Entities()[max(0,int(arg(0,0))):max(0,int(arg(0,0)))+max(1,min(4096,int(arg(1,1024))))]
        if method=='GetEntity':return r.EntityDetails(str(arg(0))) or None
        if method=='GetSelection':return list(c.SelectedEntities)
        if method=='SetSelection':
            ids=arg(0,[]);ids=[] if ids=={} else ids
            if not isinstance(ids,list) or len(ids)>4096:raise ValueError('Selection must be an entity list')
            before=tuple(c.SelectedEntities);c.SelectEntities(ids);e.Hierarchy.SetSelectedData(ids)
            if tuple(c.SelectedEntities)!=before:self.SelectionOrigins.add(owner)
            return True
        if method=='CreateEntity':return r.CreateEntity(str(arg(0,'Entity'))[:256],str(arg(1,'')))
        if method=='InstantiateModel':
            record=r.ResolveAssetRecord(str(arg(0)))
            if not record:raise ValueError('Project model asset not found')
            return r.InstantiateModelPath(record['path'],str(arg(1,'')))
        if method=='DeleteEntity':return r.DestroyEntity(str(arg(0)))
        if method=='RenameEntity':return r.Rename(str(arg(0)),str(arg(1))[:256])
        if method=='SetTransform':return r.SetTransform(str(arg(0)),Vector(arg(1),3),Vector(arg(2),4),Vector(arg(3),3))
        if method=='Translate':return r.Translate(str(arg(0)),Vector(arg(1),3))
        if method=='SetParent':return r.SetParent(str(arg(0)),str(arg(1,'')))
        if method=='AddComponent':return r.AddComponent(str(arg(0)),str(arg(1)))
        if method=='RemoveComponent':return r.RemoveComponent(str(arg(0)),str(arg(1)))
        if method=='SetComponentProperty':return r.SetComponentProperty(str(arg(0)),str(arg(1)),str(arg(2)),arg(3))
        if method=='GetAssets':return c._ProjectAssets()[max(0,int(arg(0,0))):max(0,int(arg(0,0)))+max(1,min(4096,int(arg(1,1024))))]
        if method in ('ReadAssetText','WriteAssetText','CreateMeshAsset'):
            if not r.AssetDirectory():raise ValueError('Open a project before editing assets')
            root=Path(r.AssetDirectory()).resolve();relative=Path(str(arg(0)))
            if relative.is_absolute() or '..' in relative.parts:raise ValueError('Use a path inside the project Assets folder')
            path=(root/relative).resolve()
            if not path.is_relative_to(root) or path==root or path.suffix.lower() not in {'.obj','.css','.shad','.matinst'}:raise ValueError('Unsupported project text asset')
            if method=='ReadAssetText':
                if path.stat().st_size>1024*1024:raise ValueError('Text asset exceeds read limit')
                return path.read_text(encoding='utf-8')
            if method=='CreateMeshAsset':
                if path.suffix.lower()!='.obj':raise ValueError('Generated mesh path must end in .obj')
                vertices=arg(1);indices=arg(2)
                if not isinstance(vertices,list) or not isinstance(indices,list) or not 3<=len(vertices)<=4096 or len(indices)>4096 or len(indices)%3:raise ValueError('Invalid generated mesh size')
                points=[Vector(v,3) for v in vertices]
                if any(type(i) is not int or i<0 or i>=len(points) for i in indices):raise ValueError('Invalid mesh index')
                text=''.join('v '+' '.join(format(v,'.9g') for v in point)+'\n' for point in points)+''.join('f '+' '.join(str(v+1) for v in indices[i:i+3])+'\n' for i in range(0,len(indices),3))
            else:text=str(arg(1))
            if len(text.encode('utf-8'))>1024*1024:raise ValueError('Text asset exceeds write limit')
            c.History.RecordFile(path);c.History.RecordFile(Path(str(path)+'.meta'));c.History._WriteFile(path,text.encode('utf-8'));r.RefreshAssets();e.AssetBrowser.Refresh()
            record=r.ResolveAssetRecord(str(path));return record.get('uuid') if record else str(path)
        if method=='GetAssetFolder':return str(e.AssetBrowser.CurrentFolder() or '')
        if method=='SelectAsset':
            record=r.ResolveAssetRecord(str(arg(0)))
            if not record:raise ValueError('Project asset not found')
            e.AssetBrowser.Refresh(select=Path(record['path']));c.SelectAsset(record['path']);return True
        if method=='RefreshAssets':r.RefreshAssets();e.AssetBrowser.Refresh();return True
        if method=='GetPanels':return [{'id':p.panel_id,'title':p.title,'open':e.Docking.is_panel_open(p)} for p in e.Docking.panels()]
        if method in ('OpenPanel','ClosePanel'):
            (e.Docking.activate_panel if method=='OpenPanel' else e.Docking.close_panel)(str(arg(0)));return True
        if method=='GetSceneCamera':return e.Scene.Surface.NavigationState()
        if method=='SetSceneCamera':return e.Scene.Surface.RestoreNavigationState(arg(0))
        if method=='ScreenRay':
            from PySide6.QtCore import QPoint
            ray=e.Scene.Surface._Ray(QPoint(int(arg(0)),int(arg(1))));return {'origin':[ray.Origin.X,ray.Origin.Y,ray.Origin.Z],'direction':[ray.Direction.X,ray.Direction.Y,ray.Direction.Z]}
        if method=='Raycast':
            hit=r.RaycastEditor(Vector(arg(0),3),Vector(arg(1),3));return list(hit) if hit else None
        if method=='DrawLines':
            lines=arg(0,[]);lines=[] if lines=={} else lines
            if not isinstance(lines,list) or len(lines)>2048:raise ValueError('At most 2048 tool lines are allowed')
            self.Guides[owner]=[Vector(line,10) for line in lines];self.PublishGuides();return True
        if method=='ClearLines':self.Guides.pop(owner,None);self.PublishGuides();return True
        if method=='SetSceneInput':(self.Input.add if arg(0) else self.Input.discard)(owner);return True
        if method=='BeginUndo':self.Begin(owner,str(arg(0,'Editor tool')),True);return True
        if method in ('EndUndo','CancelUndo'):self.End(owner,method=='CancelUndo');return True
        if method=='Log':e.Console.AddMessage(str(arg(0,'')),source=self.Labels.get(owner,owner));return True
        if method=='CreateDialog':
            name=str(arg(0));title=str(arg(1,name))
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,63}',name) or len(self.Dialogs)>=32 or len([k for k in self.Dialogs if k[0]==owner])>=8:raise ValueError('Invalid dialog name or dialog limit exceeded')
            if (owner,name) in self.Dialogs:raise ValueError('Dialog already exists')
            dialog=QDialog(e);dialog.setWindowTitle(title);dialog.setLayout(QVBoxLayout());dialog.setWindowModality(Qt.WindowModality.WindowModal if isinstance(arg(2),dict) and arg(2).get('modal') else Qt.WindowModality.NonModal);self.Dialogs[owner,name]=dialog;return name
        if method=='AddWidget':
            root=self.Widget(owner,arg(0));spec=arg(1,{})
            if not isinstance(root,QDialog) or not isinstance(spec,dict) or len(root.findChildren(QObject))>512:raise ValueError('Invalid widget parent or widget limit exceeded')
            name=str(spec.get('id',''));kind=spec.get('type','label');text=str(spec.get('text',''))
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,63}',name) or root.findChild(QObject,name):raise ValueError('Invalid/duplicate widget id')
            factories={'label':lambda:QLabel(text),'button':lambda:QPushButton(text),'text':lambda:QLineEdit(text),'number':QDoubleSpinBox,'checkbox':lambda:QCheckBox(text),'choice':QComboBox}
            if kind not in factories:raise ValueError('Unsupported editor widget type')
            widget=factories[kind]()
            if isinstance(widget,QLabel):widget.setTextFormat(Qt.TextFormat.PlainText)
            widget.setObjectName(name);root.layout().addWidget(widget);key=str(arg(0))+'/'+name
            if kind=='number':
                low,high,value=Vector([spec.get('min',-1e9),spec.get('max',1e9),spec.get('value',0)],3)
                if low>high:raise ValueError('Invalid number field range')
                widget.setDecimals(6);widget.setRange(low,high);widget.setValue(value)
            if kind=='choice':widget.addItems([str(v) for v in spec.get('options',[])][:256])
            if kind=='checkbox':widget.setChecked(bool(spec.get('value',False)))
            signal=getattr(widget,'clicked',None) if kind in ('button','checkbox') else getattr(widget,'editingFinished',None) if kind in ('text','number') else getattr(widget,'activated',None)
            if signal:signal.connect(lambda *_:self.Call(owner,str(spec.get('callback','OnUI')),{'widget':key,'value':self.Command(owner,'GetValue',[key])}))
            return key
        if method in ('GetValue','SetValue'):
            widget=self.Widget(owner,arg(0));setter=method=='SetValue';value=arg(1)
            if setter:
                blocked=widget.blockSignals(True)
                try:
                    if isinstance(widget,QCheckBox):widget.setChecked(bool(value))
                    elif isinstance(widget,QDoubleSpinBox):widget.setValue(Vector([value],1)[0])
                    elif isinstance(widget,QComboBox):widget.setCurrentIndex(int(value))
                    else:widget.setText(str(value))
                finally:widget.blockSignals(blocked)
                return True
            if isinstance(widget,QCheckBox):return widget.isChecked()
            if isinstance(widget,QDoubleSpinBox):return widget.value()
            if isinstance(widget,QComboBox):return widget.currentIndex()
            return widget.text()
        if method in ('ShowDialog','CloseDialog'):
            widget=self.Widget(owner,arg(0));(widget.show if method=='ShowDialog' else widget.hide)();return True
        raise ValueError('Unknown editor command: '+method)
