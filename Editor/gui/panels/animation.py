"""Animation timeline and controller editing. Preview is transactional and edit-mode only."""
import copy,math,json,bisect
from pathlib import Path
from PySide6.QtCore import Qt,Signal,QTimer,QPointF,QRectF,QSignalBlocker
from PySide6.QtGui import QColor,QPainter,QPen,QBrush,QIcon,QUndoStack,QUndoCommand
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QSplitter,QListWidget,QTreeWidget,QTreeWidgetItem,QTabWidget,QLabel,QToolButton,QFormLayout,QLineEdit,QGraphicsView,QGraphicsScene,QGraphicsRectItem,QGraphicsTextItem,QTableWidget,QTableWidgetItem,QHeaderView,QScrollArea,QStyle,QSizePolicy,QGridLayout
from ..widgets.fields import FloatInput,BoolInput,EnumInput,AssetPickerInput
from ... import animation as Data

class Change(QUndoCommand):
    def __init__(self,panel,before,after):super().__init__(panel.Tr('animation.edit'));self.Panel=panel;self.Before=before;self.After=after
    def undo(self):self.Panel.Apply(copy.deepcopy(self.Before))
    def redo(self):self.Panel.Apply(copy.deepcopy(self.After))

class StateItem(QGraphicsRectItem):
    def __init__(self,panel,state):
        super().__init__(0,0,150,54);self.Panel=panel;self.Name=state['name'];self.setFlags(self.GraphicsItemFlag.ItemIsMovable|self.GraphicsItemFlag.ItemIsSelectable|self.GraphicsItemFlag.ItemSendsGeometryChanges)
        self.setPos(*state.get('position',[30,30]));theme=panel.Themes.GetTheme();self.setBrush(QColor(theme.surface));self.setPen(QPen(QColor(theme.accent if self.Name in (panel.Graph.get('entry'),panel.ActiveState) else theme.border),2))
        text=QGraphicsTextItem(self.Name,self);text.setDefaultTextColor(QColor(theme.text));text.setPos(8,6);text.setTextWidth(132)
    def itemChange(self,change,value):
        result=super().itemChange(change,value)
        if change==self.GraphicsItemChange.ItemPositionHasChanged and not self.Panel._updating:self.Panel.UpdateEdges()
        return result
    def mouseReleaseEvent(self,event):
        super().mouseReleaseEvent(event);position=[self.pos().x(),self.pos().y()];old=next((s for s in self.Panel.Graph['states'] if s['name']==self.Name),None)
        if old and old.get('position')!=position:
            panel=self.Panel;name=self.Name;QTimer.singleShot(0,panel,lambda:panel.Edit(lambda graph:next(s for s in graph['states'] if s['name']==name).__setitem__('position',position)))

class Timeline(QWidget):
    Seek=Signal(float)
    def __init__(self,panel):super().__init__();self.Panel=panel;self.Info={};self.Time=0;self.Zoom=1;self.setMinimumHeight(160);self.setMouseTracking(True)
    def paintEvent(self,event):
        theme=self.Panel.Themes.GetTheme();p=QPainter(self);p.fillRect(self.rect(),QColor(theme.input_background));duration=max(.001,float(self.Info.get('duration',1)));left=160;width=max(1,self.width()-left-12);scale=width/duration*self.Zoom;p.setPen(QColor(theme.text_muted))
        for i in range(11):
            t=duration*i/10/self.Zoom;x=left+t*scale;p.drawLine(int(x),0,int(x),self.height());p.drawText(QRectF(x+3,0,58,20),f'{t:.2f}')
        bones=self.Panel.Info.get('bones',[])
        tracks=self.Info.get('tracks',[]);first=max(0,(event.rect().top()-48)//23);last=min(len(tracks),(event.rect().bottom()+23)//23)
        for row in range(first,last):
            track=tracks[row]
            y=36+row*23
            if y>self.height():break
            index=track['bone'];name=bones[index]['name'] if index<len(bones) else str(index);channel=self.Panel.Tr(('animation.position','animation.rotation','animation.scale')[track['channel']]);p.setPen(QColor(theme.text));p.drawText(QRectF(6,y-10,148,20),Qt.AlignmentFlag.AlignLeft|Qt.AlignmentFlag.AlignVCenter,p.fontMetrics().elidedText(name+' Â· '+channel,Qt.TextElideMode.ElideRight,148));p.setPen(QColor(theme.accent));p.setBrush(QColor(theme.accent))
            keys=track['keys'];begin=bisect.bisect_left(keys,0);end=bisect.bisect_right(keys,(self.width()-left)/scale);stride=max(1,(end-begin)//max(1,int(width)*2))
            for time in keys[begin:end:stride]:
                x=left+time*scale
                if left<=x<=self.width():p.drawRect(QRectF(x-3,y-3,6,6))
        p.setPen(QPen(QColor(theme.accent),2));x=left+self.Time*scale;p.drawLine(int(x),20,int(x),self.height())
    def mousePressEvent(self,event):self._Seek(event.position().x())
    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.LeftButton:self._Seek(event.position().x())
    def _Seek(self,x):
        duration=max(.001,float(self.Info.get('duration',1)));time=max(0,min(duration,(x-160)/max(1,self.width()-172)*duration/self.Zoom));self.Seek.emit(time)

class AnimationPanel(QWidget):
    AssetChanged=Signal(object)
    def __init__(self,runtime,locale,resources,themes,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Locale=locale;self.Resources=resources;self.Themes=themes;self.Path=None;self.Graph=None;self.Entity='';self.Info={};self.Selected='';self.Parameters={};self.ControllerPreview=False;self.ActiveState='';self.AssetProvider=lambda:[];self._updating=False;self.History=QUndoStack(self);self.History.setUndoLimit(100);self.Timer=QTimer(self);self.Timer.setInterval(33);self.Timer.timeout.connect(self.Advance)
        layout=QVBoxLayout(self);layout.setContentsMargins(6,6,6,6);bar=QHBoxLayout();layout.addLayout(bar);self.Title=QLabel();self.Title.setMinimumWidth(0);self.Title.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Preferred);bar.addWidget(self.Title,1)
        self.Play=self.Button('animation.play',self.Toggle);self.Stop=self.Button('animation.stop',self.EndPreview);self.AddState=self.Button('animation.add_state',self.NewState);self.AddTransition=self.Button('animation.add_transition',self.NewTransition);self.Delete=self.Button('animation.delete',self.DeleteSelected)
        for button in (self.Play,self.Stop,self.AddState,self.AddTransition,self.Delete):bar.addWidget(button)
        self.Tabs=QTabWidget();layout.addWidget(self.Tabs,1);self.ClipPage=QWidget();self.GraphPage=QWidget();self.Tabs.addTab(self.ClipPage,self.Tr('animation.clips'));self.Tabs.addTab(self.GraphPage,self.Tr('animation.controller'))
        clipLayout=QHBoxLayout(self.ClipPage);split=QSplitter();clipLayout.addWidget(split);left=QWidget();ll=QVBoxLayout(left);ll.setContentsMargins(0,0,0,0);self.Clips=QListWidget();self.Bones=QTreeWidget();self.Bones.setHeaderHidden(True);ll.addWidget(self.Clips);ll.addWidget(self.Bones);split.addWidget(left);right=QWidget();rl=QVBoxLayout(right);self.Timeline=Timeline(self);timelineScroll=QScrollArea();timelineScroll.setWidgetResizable(True);timelineScroll.setWidget(self.Timeline);rl.addWidget(timelineScroll,1);timebar=QGridLayout();rl.addLayout(timebar);self.Time=FloatInput(0,86400,0,3);self.Speed=FloatInput(0,10,1);self.Zoom=FloatInput(.25,10,1)
        for column,(key,field) in enumerate((('time',self.Time),('speed',self.Speed),('zoom',self.Zoom))):
            field.setMaximumWidth(88);timebar.addWidget(QLabel(self.Tr('animation.'+key)),0,column);timebar.addWidget(field,1,column)
        split.addWidget(right);split.setStretchFactor(1,1)
        graphLayout=QHBoxLayout(self.GraphPage);gs=QSplitter();graphLayout.addWidget(gs);self.Scene=QGraphicsScene(self);self.Canvas=QGraphicsView(self.Scene);gs.addWidget(self.Canvas);scroll=QScrollArea();scroll.setWidgetResizable(True);self.Fields=QWidget();self.Form=QFormLayout(self.Fields);self.Form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow);scroll.setWidget(self.Fields);gs.addWidget(scroll);gs.setStretchFactor(0,1)
        self.Error=QLabel();self.Error.setWordWrap(True);layout.addWidget(self.Error);self.Clips.currentRowChanged.connect(self.ChooseClip);self.Time.valueChanged.connect(self.Seek);self.Timeline.Seek.connect(self.Time.setValue);self.Zoom.valueChanged.connect(lambda v:(setattr(self.Timeline,'Zoom',v),self.Timeline.update()));self.Scene.selectionChanged.connect(self.Inspect);self.Themes.ThemeChanged.connect(lambda *_:(self.Rebuild(),self.UpdateToolbarIcons()));self.Locale.LocaleChanged.connect(self.Retranslate);self.Retranslate()
    def Tr(self,key):return self.Locale.Translate(key)
    def Button(self,key,callback):
        b=QToolButton();b.setText(self.Tr(key));b.setToolTip(self.Tr(key));b.clicked.connect(callback);return b
    def Retranslate(self,*_):
        for widget,key in ((self.Play,'play'),(self.Stop,'stop'),(self.AddState,'add_state'),(self.AddTransition,'add_transition'),(self.Delete,'delete')):widget.setText(self.Tr('animation.'+key))
        self.Tabs.setTabText(0,self.Tr('animation.clips'));self.Tabs.setTabText(1,self.Tr('animation.controller'));self.Title.setText(self.Path.name if self.Path else self.Tr('animation.select_model'));self.Rebuild();self.UpdateToolbarIcons()
    def UpdateToolbarIcons(self):
        theme=self.Themes.GetTheme();name='light' if theme.background=='#d4d4d4' else 'dark'
        for button,standard in ((self.Play,QStyle.StandardPixmap.SP_MediaPlay),(self.Stop,QStyle.StandardPixmap.SP_MediaStop),(self.Delete,QStyle.StandardPixmap.SP_TrashIcon)):
            pixmap=self.style().standardIcon(standard).pixmap(18,18);painter=QPainter(pixmap);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),QColor(theme.text));painter.end();button.setIcon(QIcon(pixmap))
        for button,icon in ((self.AddState,'animation_state'),(self.AddTransition,'animation_transition')):button.setIcon(self.Resources.Icon(f'icons/{name}/{icon}.svg'))
        for button in (self.Play,self.Stop,self.AddState,self.AddTransition,self.Delete):button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly);button.setToolTip(button.text());button.setAccessibleName(button.text())
    def SetEntity(self,identity):
        identity=str(identity or '')
        if identity==self.Entity:return
        self.EndPreview();self.Entity=identity;self.Info={}
        if self.isVisible():self.Refresh()
    def showEvent(self,event):super().showEvent(event);self.Refresh()
    def hideEvent(self,event):self.EndPreview();super().hideEvent(event)
    def Refresh(self):
        self.Clips.clear();self.Bones.clear()
        if not self.Entity:return
        try:
            self.Info=self.Runtime.AnimationInfo(self.Entity);items={}
            for row in self.Info.get('clips',[]):self.Clips.addItem(row['name'])
            for bone in self.Info.get('bones',[]):item=QTreeWidgetItem([bone['name']]);items[bone['index']]=item
            for bone in self.Info.get('bones',[]):
                item=items[bone['index']];parent=items.get(bone['parent'])
                if parent:parent.addChild(item)
                else:self.Bones.addTopLevelItem(item)
            if self.Clips.count():self.Clips.setCurrentRow(0)
            self.Error.clear()
        except (ValueError,RuntimeError,IndexError) as error:self.Error.setText(str(error))
    def ChooseClip(self,row):
        clips=self.Info.get('clips',[]);self.Timeline.Info=clips[row] if 0<=row<len(clips) else {};self.Time.setMaximum(max(0,float(self.Timeline.Info.get('duration',0))));self.Timeline.setMinimumHeight(max(160,36+23*len(self.Timeline.Info.get('tracks',[]))));self.Timeline.update()
    def Seek(self,time):
        self.Timeline.Time=time;self.Timeline.update()
        if not self.Entity or not self.Timeline.Info:return
        try:self.Runtime.PreviewAnimation(self.Entity,self.Timeline.Info['name'],time);self.Error.clear()
        except (ValueError,RuntimeError,IndexError) as error:self.Timer.stop();self.Error.setText(str(error))
    def Toggle(self):
        if self.Timer.isActive():self.Timer.stop()
        elif self.Tabs.currentWidget() is self.GraphPage and self.Graph and self.Entity:
            try:self.Runtime.PreviewAnimationController(self.Entity,json.dumps(self.Graph),0,self.Parameters,True);self.ControllerPreview=True;self.Timer.start()
            except (ValueError,RuntimeError,AttributeError) as error:self.Error.setText(str(error))
        elif self.Timeline.Info:self.ControllerPreview=False;self.Timer.start()
    def Advance(self):
        if self.ControllerPreview:
            try:
                status=self.Runtime.PreviewAnimationController(self.Entity,json.dumps(self.Graph),min(1,self.Timer.interval()*.001*self.Speed.value()),self.Parameters,False);self.ActiveState=status.get('state','');self.Title.setText(self.ActiveState)
                theme=self.Themes.GetTheme()
                for name,item in self.Items.items():item.setPen(QPen(QColor(theme.accent if name==self.ActiveState else theme.border),2))
            except (ValueError,RuntimeError,AttributeError) as error:self.Timer.stop();self.Error.setText(str(error))
            return
        duration=float(self.Timeline.Info.get('duration',0));nextTime=self.Time.value()+self.Timer.interval()*.001*self.Speed.value();self.Time.setValue(nextTime%duration if duration>0 else 0)
    def EndPreview(self):
        self.Timer.stop();self.ControllerPreview=False;self.ActiveState=''
        blocker=QSignalBlocker(self.Time);self.Time.setValue(0);del blocker;self.Timeline.Time=0;self.Timeline.update()
        try:self.Runtime.EndAnimationPreview()
        except (RuntimeError,ValueError):pass
    def Open(self,path):
        self.EndPreview()
        try:self.Graph=Data.Load(path);self.Path=Path(path);self.History.clear();self.Rebuild();self.Title.setText(self.Path.name);self.Tabs.setCurrentWidget(self.GraphPage);self.Error.clear();return True
        except (OSError,ValueError) as error:self.Error.setText(str(error));return False
    def Apply(self,graph):
        self.EndPreview();self.Graph=graph;self.Rebuild()
        try:
            if self.Path and Data.Save(self.Path,graph):self.AssetChanged.emit(self.Path)
            self.Error.clear()
        except (OSError,ValueError) as error:self.Error.setText(str(error))
    def Flush(self):
        if not self.Path or self.Graph is None:return True
        try:
            if Data.Save(self.Path,self.Graph):self.AssetChanged.emit(self.Path)
            return True
        except (OSError,ValueError) as error:self.Error.setText(str(error));return False
    def Edit(self,operation):
        if not self.Graph:return
        before=copy.deepcopy(self.Graph);after=copy.deepcopy(before)
        try:operation(after);Data.Validate(after)
        except (ValueError,KeyError,TypeError) as error:self.Error.setText(str(error));return
        if before!=after:self.History.push(Change(self,before,after))
    def Rebuild(self):
        self._updating=True;self.Scene.blockSignals(True);self.Scene.clear();self.Items={};self.Edges=[]
        theme=self.Themes.GetTheme();self.Scene.setBackgroundBrush(QColor(theme.input_background))
        if self.Graph:
            for i,state in enumerate(self.Graph['states']):
                if 'position' not in state:state['position']=[30+(i%3)*210,30+(i//3)*110]
                item=StateItem(self,state);self.Scene.addItem(item);self.Items[state['name']]=item
            for transition in self.Graph.get('transitions',[]):
                a=self.Items.get(transition['from']);b=self.Items.get(transition['to'])
                if a and b:
                    start=a.sceneBoundingRect().center();end=b.sceneBoundingRect().center();line=self.Scene.addLine(start.x(),start.y(),end.x(),end.y(),QPen(QColor(theme.accent),2));line.setZValue(-1);label=self.Scene.addText(f"{transition.get('duration',.2):g}s");label.setDefaultTextColor(QColor(theme.text_muted));label.setPos((start+end)*.5);self.Edges.append((transition['from'],transition['to'],line,label))
            if self.Selected in self.Items:self.Items[self.Selected].setSelected(True)
        self.Scene.blockSignals(False);self._updating=False;self.Inspect()
    def UpdateEdges(self):
        for source,target,line,label in getattr(self,'Edges',[]):
            a=self.Items[source].sceneBoundingRect().center();b=self.Items[target].sceneBoundingRect().center();line.setLine(a.x(),a.y(),b.x(),b.y());label.setPos((a+b)*.5)
    def NewState(self):
        def Add(graph):
            names={s['name'] for s in graph['states']};i=1
            while 'State '+str(i) in names:i+=1
            graph['states'].append(dict(name='State '+str(i),clip=self.Clips.currentItem().text() if self.Clips.currentItem() else '',speed=1.,loop=True))
        self.Edit(Add)
    def NewTransition(self):
        if not self.Graph or len(self.Graph['states'])<2:return
        source=self.Selected or self.Graph['entry'];target=next(s['name'] for s in self.Graph['states'] if s['name']!=source);self.Edit(lambda g:g.setdefault('transitions',[]).append({'from':source,'to':target,'duration':.2,'exitTime':1.,'conditions':[]}))
        # Python's keyword spelling is normalized to the document field.
    def DeleteSelected(self):
        if not self.Graph or len(self.Graph['states'])<2 or not self.Selected:return
        name=self.Selected
        def Remove(g):
            g['states']=[s for s in g['states'] if s['name']!=name];g['transitions']=[t for t in g.get('transitions',[]) if t['from']!=name and t['to']!=name]
            if g['entry']==name:g['entry']=g['states'][0]['name']
        self.Selected='';self.Edit(Remove)
    def Inspect(self):
        if self._updating:return
        while self.Form.rowCount():
            row=self.Form.takeRow(0)
            for part in (row.labelItem,row.fieldItem):
                if part and part.widget():part.widget().deleteLater()
        if not self.Graph:return
        selected=self.Scene.selectedItems();self.Selected=selected[0].Name if selected else self.Selected;state=next((s for s in self.Graph['states'] if s['name']==self.Selected),None)
        source=AssetPickerInput(self.Tr('properties.select_project_asset'),accepted_extensions={'.glb','.gltf'});source.AssetResolver=getattr(self.Runtime,'ResolveAssetRecord',None);source.ConfigureAssets(list(self.AssetProvider()));source.SetValue(self.Graph.get('sourceModel',''));source.PickRequested.connect(source.OpenProjectPicker);source.ValueChanged.connect(lambda v:self.Edit(lambda g:g.__setitem__('sourceModel',v or '00000000-0000-0000-0000-000000000000')));self.Form.addRow(self.Tr('animation.retarget_source'),source)
        scale=FloatInput(.000001,1000,self.Graph.get('translationScale',1));scale.valueChanged.connect(lambda v:self.Edit(lambda g:g.__setitem__('translationScale',v)));self.Form.addRow(self.Tr('animation.retarget_scale'),scale)
        def AutoMap():
            try:
                mapping=self.Runtime.AnimationRetargetMap(self.Entity,self.Graph.get('sourceModel',''));self.Edit(lambda g:g.__setitem__('retarget',mapping))
            except (ValueError,RuntimeError,AttributeError) as error:self.Error.setText(str(error))
        self.Form.addRow(self.Button('animation.auto_map',AutoMap))
        mapping=self.Graph.get('retarget',[])
        if mapping:
            table=QTableWidget(len(mapping),3);table.setHorizontalHeaderLabels([self.Tr('animation.source_bone'),self.Tr('animation.target_bone'),self.Tr('animation.copy_translation')]);table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
            for i,row in enumerate(mapping):
                for col,key in enumerate(('source','target','copyTranslation')):table.setItem(i,col,QTableWidgetItem(str(row.get(key,False))))
            def SaveMap():
                try:self.Edit(lambda g:g.__setitem__('retarget',[dict(source=int(table.item(i,0).text()),target=int(table.item(i,1).text()),copyTranslation=table.item(i,2).text().lower()=='true') for i in range(table.rowCount())]))
                except (ValueError,AttributeError):pass
            table.cellChanged.connect(lambda *_:SaveMap());self.Form.addRow(table)
        parameters=set()
        for s in self.Graph['states']:
            if s.get('blend'):parameters.update((s.get('parameterX',''),s.get('parameterY','')))
        for t in self.Graph.get('transitions',[]):parameters.update(c['parameter'] for c in t.get('conditions',[]))
        for name in sorted(parameters-{''}):
            field=FloatInput(value=self.Parameters.get(name,0));field.valueChanged.connect(lambda v,k=name:self.Parameters.__setitem__(k,v));self.Form.addRow(self.Tr('animation.preview_parameter')+' '+name,field)
        if not state:return
        name=state['name']
        def Set(key,value):self.Edit(lambda g:next(s for s in g['states'] if s['name']==name).__setitem__(key,value))
        entry=BoolInput(self.Graph['entry']==name);entry.ValueChanged.connect(lambda v:self.Edit(lambda g:g.__setitem__('entry',name)) if v else None);self.Form.addRow(self.Tr('animation.entry'),entry)
        rename=QLineEdit(name)
        def Rename():
            new=rename.text().strip()
            def ChangeName(g):
                next(s for s in g['states'] if s['name']==name)['name']=new
                if g['entry']==name:g['entry']=new
                for t in g.get('transitions',[]):
                    for k in ('from','to'):
                        if t[k]==name:t[k]=new
            self.Selected=new;self.Edit(ChangeName)
        rename.editingFinished.connect(Rename);self.Form.addRow(self.Tr('animation.name'),rename)
        clip=QLineEdit(state.get('clip',''));clip.editingFinished.connect(lambda:Set('clip',clip.text()));self.Form.addRow(self.Tr('animation.clip'),clip)
        speed=FloatInput(0,100,state.get('speed',1));speed.valueChanged.connect(lambda v:Set('speed',v));self.Form.addRow(self.Tr('animation.speed'),speed);loop=BoolInput(state.get('loop',True));loop.ValueChanged.connect(lambda v:Set('loop',v));self.Form.addRow(self.Tr('animation.loop'),loop)
        blend=EnumInput();blend.SetOptions([(self.Tr('animation.'+key),i) for i,key in enumerate(('single_clip','blend_1d','blend_2d'))]);blend.SetValue(state.get('blend',0))
        def SetBlend(value):
            def Op(g):
                s=next(s for s in g['states'] if s['name']==name);s['blend']=value
                if value:s.setdefault('parameterX','Speed');s.setdefault('parameterY','Direction');s.setdefault('samples',[dict(clip=s.get('clip') or 'Idle',x=0.,y=0.,speed=1.)])
            self.Edit(Op)
        blend.currentIndexChanged.connect(lambda _:SetBlend(blend.GetValue()));self.Form.addRow(self.Tr('animation.blend'),blend)
        if state.get('blend',0):
            for key in ('parameterX','parameterY'):
                edit=QLineEdit(state.get(key,''));edit.editingFinished.connect(lambda k=key,e=edit:Set(k,e.text()));self.Form.addRow(self.Tr('animation.'+key),edit)
            samples=QTableWidget(len(state.get('samples',[])),4);samples.setHorizontalHeaderLabels([self.Tr('animation.clip'),'X','Y',self.Tr('animation.speed')]);samples.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
            for i,sample in enumerate(state.get('samples',[])):
                for col,key in enumerate(('clip','x','y','speed')):samples.setItem(i,col,QTableWidgetItem(str(sample.get(key,'' if key=='clip' else 0))))
            def Samples():
                try:Set('samples',[dict(clip=samples.item(i,0).text(),x=float(samples.item(i,1).text()),y=float(samples.item(i,2).text()),speed=float(samples.item(i,3).text())) for i in range(samples.rowCount())])
                except (ValueError,AttributeError):pass
            samples.cellChanged.connect(lambda *_:Samples());self.Form.addRow(samples);add=self.Button('animation.add_sample',lambda:Set('samples',state.get('samples',[])+[dict(clip=state.get('clip') or 'Idle',x=1.,y=0.,speed=1.)]));self.Form.addRow(add)
        for index,t in enumerate(self.Graph.get('transitions',[])):
            if t['from']!=name:continue
            self.Form.addRow(QLabel(self.Tr('animation.transition')))
            target=EnumInput();target.SetOptions([(s['name'],s['name']) for s in self.Graph['states']]);target.SetValue(t['to']);target.currentIndexChanged.connect(lambda _,i=index,e=target:self.Edit(lambda g:g['transitions'][i].__setitem__('to',e.GetValue())));self.Form.addRow(self.Tr('animation.target'),target)
            for key,low,high,default in (('duration',0,60,.2),('exitTime',-1,1000,-1)):
                edit=FloatInput(low,high,t.get(key,default));edit.valueChanged.connect(lambda v,k=key,i=index:self.Edit(lambda g:g['transitions'][i].__setitem__(k,v)));self.Form.addRow(self.Tr('animation.'+key),edit)
            for row,condition in enumerate(t.get('conditions',[])):
                parameter=QLineEdit(condition['parameter']);parameter.editingFinished.connect(lambda e=parameter,i=index,r=row:self.Edit(lambda g:g['transitions'][i]['conditions'][r].__setitem__('parameter',e.text())));self.Form.addRow(self.Tr('animation.parameter'),parameter);comparison=EnumInput();comparison.SetOptions([(text,i) for i,text in enumerate(('>','<','==','!='))]);comparison.SetValue(condition.get('comparison',0));comparison.currentIndexChanged.connect(lambda _,e=comparison,i=index,r=row:self.Edit(lambda g:g['transitions'][i]['conditions'][r].__setitem__('comparison',e.GetValue())));self.Form.addRow(self.Tr('animation.comparison'),comparison);value=FloatInput(value=condition.get('value',0));value.valueChanged.connect(lambda v,i=index,r=row:self.Edit(lambda g:g['transitions'][i]['conditions'][r].__setitem__('value',v)));self.Form.addRow(self.Tr('animation.value'),value)
            add=self.Button('animation.add_condition',lambda _,i=index:self.Edit(lambda g:g['transitions'][i].setdefault('conditions',[]).append(dict(parameter='Speed',comparison=0,value=.5))));self.Form.addRow(add);remove=self.Button('animation.remove_transition',lambda _,i=index:self.Edit(lambda g:g['transitions'].pop(i)));self.Form.addRow(remove)
