"""Scene-local terrain authoring. One stroke is one undo command."""
from math import cos,sin,pi,isfinite
from time import monotonic
from PySide6.QtCore import QObject,QEvent,QPoint,Qt,QTimer,QSize
from PySide6.QtGui import QPainter,QColor,QIcon
from PySide6.QtWidgets import QWidget,QFrame,QVBoxLayout,QHBoxLayout,QGridLayout,QFormLayout,QToolButton,QComboBox,QDialog,QLabel,QPushButton,QScrollArea
from .widgets.collections import TabInput
from ..materials import ZERO
from .widgets.fields import FloatInput,IntInput
from ..resources import ResourceManager

class TerrainHandle(QWidget):
    def __init__(self,owner):super().__init__(owner.Overlay);self.Owner=owner;self.setFixedHeight(16);self._start=None
    def paintEvent(self,event):
        p=QPainter(self);p.setPen(Qt.PenStyle.NoPen);p.setBrush(self.palette().text());cx=self.width()//2
        for y in (5,10):
            for x in (cx-5,cx,cx+5):p.drawEllipse(QPoint(x,y),1,1)
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self._start=(event.globalPosition().toPoint(),self.Owner.Overlay.pos());event.accept()
    def mouseMoveEvent(self,event):
        if self._start:self.Owner.Place(self._start[1]+event.globalPosition().toPoint()-self._start[0]);event.accept()
    def mouseReleaseEvent(self,event):self._start=None;self.Owner.SavePosition()

class TerrainEditor(QObject):
    def __init__(self,controller):
        super().__init__(controller);self.Controller=controller;self.Window=controller.Window;self.Runtime=controller.Runtime;self.Surface=self.Window.Scene.Surface;self.Tr=self.Window.Localization.Translate
        self._point=QPoint();self._modifiers=Qt.KeyboardModifier.NoModifier
        self._stroke_timer=QTimer(self);self._stroke_timer.setInterval(33);self._stroke_timer.timeout.connect(self.StrokeTick)
        self.Active=False;self.Stroke=False;self._last=0.;self.Tool=0;self.Dialog=None
        self.Overlay=None;self.Island=None;self.Target=None;self.Buttons=[]
        self.SplineEditor=None
        self.Resources=ResourceManager()
        self.Surface.installEventFilter(self)
        self.Window.Scene.TerrainAction.toggled.connect(self.SetActive);self.Runtime.SceneChanged.connect(self.RefreshTargets);self.Runtime.ProjectChanging.connect(self.Stop)
        self.Window.Localization.LocaleChanged.connect(self.Retranslate)
        self.State={'radius':1.,'strength':2.,'falloff':.5,'height':0.,'layer':0,'prototype':0,'spline':0,'stamp':ZERO,'scale':10.}
        saved=self.Window._settings.get('terrain_brush',{})
        for key in self.State:
            value=saved.get(key)
            if type(value)==type(self.State[key]) and (not isinstance(value,(int,float)) or isfinite(value)):self.State[key]=value
        for key,(low,high) in {'radius':(.01,10000),'strength':(0,10000),'falloff':(0,1),'height':(-100000,100000),'scale':(-100000,100000),'layer':(0,31),'prototype':(0,63),'spline':(0,127)}.items():self.State[key]=max(low,min(high,self.State[key]))
        if self.State['stamp']=='0':self.State['stamp']=ZERO
        self.Window.ThemeManager.ThemeChanged.connect(self.ApplyTheme);self.ApplyTheme();self.RefreshTargets();self.Place()
    def _CreateOverlay(self):
        self.Overlay=QFrame(self.Surface);self.Overlay.setObjectName('TerrainOverlay');self.Overlay.setMaximumWidth(152)
        outer=QVBoxLayout(self.Overlay);outer.setContentsMargins(6,3,6,6);outer.setSpacing(3);outer.addWidget(TerrainHandle(self))
        body=QWidget();layout=QVBoxLayout(body);layout.setContentsMargins(0,0,0,0);layout.setSpacing(3)
        self.ToolScroll=QScrollArea();self.ToolScroll.setFrameShape(QFrame.Shape.NoFrame);self.ToolScroll.setWidgetResizable(True);self.ToolScroll.setWidget(body);outer.addWidget(self.ToolScroll)
        self.Target=QComboBox();self.Target.currentIndexChanged.connect(self._SplineTargetChanged);layout.addWidget(self.Target)
        tools=QWidget(body);grid=QGridLayout(tools);grid.setContentsMargins(0,0,0,0);grid.setSpacing(3);layout.addWidget(tools)
        self.Buttons=[]
        for key,tool in (('raise',0),('lower',1),('smooth',2),('flatten',3),('paint',4),('hole',6),('vegetation',8),('spline',9)):
            button=QToolButton(tools);button.setObjectName('TerrainTool');button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly);button.setToolTip(self.Tr('terrain.'+key));button.setAccessibleName(self.Tr('terrain.'+key));button.setCheckable(True);button.setChecked(tool==self.Tool);button.setIconSize(QSize(20,20));button.setFixedSize(32,32);button.clicked.connect(lambda _,t=tool:self.SetTool(t));grid.addWidget(button,len(self.Buttons)//4,len(self.Buttons)%4);self.Buttons.append((button,key,tool))
        self.Island=QToolButton(self.Overlay);self.Island.setObjectName('TerrainSettings');self.Island.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly);self.Island.setIconSize(QSize(20,20));self.Island.setToolTip(self.Tr('terrain.settings'));self.Island.setAccessibleName(self.Tr('terrain.settings'));self.Island.clicked.connect(self.OpenSettings);self.Island.setFixedSize(32,28);footer=QHBoxLayout();footer.setContentsMargins(0,0,0,0)
        self.SplineControls=QToolButton(self.Overlay);self.SplineControls.setObjectName('TerrainSettings');self.SplineControls.setFixedSize(32,28);self.SplineControls.setIconSize(QSize(20,20));self.SplineControls.setToolTip(self.Tr('terrain.toggle_spline_controls'));self.SplineControls.clicked.connect(lambda:self.SplineEditor.ToggleControls() if self.SplineEditor else None);self.SplineControls.setVisible(self.Tool==9)
        footer.addWidget(self.SplineControls);footer.addStretch();footer.addWidget(self.Island);outer.addLayout(footer)
        self.Overlay.resize(152,156);self.Overlay.hide()
        self.ApplyTheme()

    def ApplyTheme(self,*_):
        if self.Overlay is None:return
        theme=self.Window.ThemeManager.GetTheme() if hasattr(self.Window.ThemeManager,'GetTheme') else None
        text=QColor(theme.text) if theme else self.Window.palette().text().color()
        accent=QColor(theme.accent) if theme else self.Window.palette().highlight().color()
        def icon(name):
            base=self.Resources.Icon('icons/terrain/'+name+'.svg')
            def tinted(color):
                pixmap=base.pixmap(40,40);painter=QPainter(pixmap);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),color);painter.end();return pixmap
            result=QIcon(tinted(text));result.addPixmap(tinted(accent),QIcon.Mode.Normal,QIcon.State.On);return result
        for button,key,_ in self.Buttons:button.setIcon(icon(key))
        self.Island.setIcon(icon('settings'))
        self.SplineControls.setIcon(icon('controls'))
        if self.SplineEditor is not None:
            for widget,name in ((self.SplineEditor.New,'new'),(self.SplineEditor.Continue,'continue'),(self.SplineEditor.Remove,'remove'),(self.SplineEditor.Finish,'finish')):widget.setIcon(icon(name));widget.setIconSize(QSize(18,18))
    def Retranslate(self,*_):
        for button,key,_ in self.Buttons:button.setToolTip(self.Tr('terrain.'+key));button.setAccessibleName(self.Tr('terrain.'+key))
        if self.Island:self.Island.setToolTip(self.Tr('terrain.settings'));self.Island.setAccessibleName(self.Tr('terrain.settings'))
        if self.Overlay is not None:self.SplineControls.setToolTip(self.Tr("terrain.toggle_spline_controls"))
        if self.SplineEditor is not None:self.SplineEditor.Retranslate()
    def _SplineTargetChanged(self,*_):
        if self.SplineEditor is not None and self.Active and self.Tool==9:
            self.SplineEditor.Refresh()
            self.Controller._UpdateGizmo()

    def SetTool(self,tool):
        self.FinishStroke()
        if self.Tool==9 and tool!=9 and self.SplineEditor is not None:self.SplineEditor.Deactivate()
        self.Tool=tool
        if self.Overlay is not None:self.SplineControls.setVisible(tool==9)
        for button,_,value in self.Buttons:button.setChecked(value==tool)
        if tool==9 and self.Active:
            if self.SplineEditor is None:
                from .terrain_spline import TerrainSplineEditor
                self.SplineEditor=TerrainSplineEditor(self)
            self.ApplyTheme()
            self.SplineEditor.Activate()
        else:self.Controller._UpdateGizmo()
    def RefreshTargets(self,*_):
        if not self.Active or self.Target is None:return
        previous=self.Target.currentData();self.Target.blockSignals(True);self.Target.clear()
        try:
            for row in self.Runtime.Entities():
                identity=str(row['uuid']);details=self.Runtime.EntityDetails(identity)
                if 'Terrain' in details.get('components',[]):self.Target.addItem(row.get('name',self.Tr('terrain.title')),identity)
        except (RuntimeError,ValueError):pass
        index=self.Target.findData(self.Controller.SelectedEntity or previous)
        if index>=0:self.Target.setCurrentIndex(index)
        self.Target.blockSignals(False)
        self._SplineTargetChanged()
    def SetActive(self,active):
        self.FinishStroke()
        if not active and self.SplineEditor is not None:self.SplineEditor.Deactivate()
        self.Active=bool(active)
        if self.Active and self.Overlay is None:self._CreateOverlay()
        if self.Overlay is None:return
        if active:
            if self.Surface._attached and not self.Surface._captured:
                self.Overlay.setAttribute(Qt.WidgetAttribute.WA_DontCreateNativeAncestors);self.Overlay.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
            self.RefreshTargets();self.Place();self.Overlay.show();self.Overlay.raise_()
            if self.Tool==9:self.SetTool(9)
        else:
            self.Overlay.hide()
            self.Runtime.SetToolGuides([])
            if self.Dialog:self.Dialog.close()
            if hasattr(self.Controller,"_UpdateGizmo"):self.Controller._UpdateGizmo()
    def Stop(self,*_):self.Window.Scene.TerrainAction.setChecked(False)
    def Place(self,position=None):
        if self.Overlay is None:return
        if position is None:
            saved=self.Window._settings.get('terrain_overlay',{})
            try:position=QPoint(int(saved.get('x',self.Surface.width()-self.Overlay.width()-14)),int(saved.get('y',86)))
            except (TypeError,ValueError,OverflowError):position=QPoint(self.Surface.width()-self.Overlay.width()-14,86)
        size=QSize(min(152,max(80,self.Surface.width()-8)),min(156,max(60,self.Surface.height()-16)))
        if self.Overlay.size()!=size:self.Overlay.resize(size)
        max_x=max(0,self.Surface.width()-self.Overlay.width()-8);max_y=max(0,self.Surface.height()-self.Overlay.height()-8)
        self.Overlay.move(max(0,min(max_x,position.x())),max(0,min(max_y,position.y())))
        if self.SplineEditor is not None:self.SplineEditor.Place()
    def SavePosition(self):
        if self.Overlay is None:return
        self.Window._settings['terrain_overlay']={'x':self.Overlay.x(),'y':self.Overlay.y()};self._Save()
    def _Save(self):
        self.Window._settings['terrain_brush']=dict(self.State)
        if self.Window._settings_saver:self.Window._settings_saver(self.Window._settings)
    def _Target(self):
        identity=self.Target.currentData() if self.Target is not None else None
        return identity if identity and not self.Runtime.IsEditorLocked(identity) else ''
    def _Values(self,point):
        ray=self.Surface._Ray(point);return dict(self.State,origin=(ray.Origin.X,ray.Origin.Y,ray.Origin.Z),direction=(ray.Direction.X,ray.Direction.Y,ray.Direction.Z),tool=self.Tool,seconds=.05)
    def _Apply(self,point,modifiers):
        identity=self._Target()
        if not identity:return
        command='vegetation' if self.Tool==8 else 'spline_point' if self.Tool==9 else 'brush';values=self._Values(point)
        if modifiers&Qt.KeyboardModifier.ShiftModifier:
            if self.Tool==0:values['tool']=1
            elif self.Tool==1:values['tool']=0
            elif self.Tool==4:values['tool']=5
            elif self.Tool==6:values['tool']=7
            elif self.Tool==8:values['erase']=True
        now=monotonic();values['seconds']=min(.1,max(.001,now-self._last)) if self._last else .033;self._last=now
        values['count']=1
        try:
            result=self.Runtime.TerrainCommand(identity,command,values)
            if result.get('changed'):self.MarkDirty(identity)
        except Exception as error:
            self.Window.Console.AddMessage(str(error),source='Terrain');self.FinishStroke()
    def StrokeTick(self):
        if self.Stroke and self.Tool!=9:self._Apply(self._point,self._modifiers)
    def FinishStroke(self):
        self._stroke_timer.stop()

        if self.Stroke:
            self.Stroke=False
            if self.Controller.History.Commit():self.MarkDirty(self._Target())
    def eventFilter(self,watched,event):
        if event.type()==QEvent.Type.Resize:self.Place(self.Overlay.pos() if self.Overlay else None)
        if not self.Active:return False
        if self.Tool==9 and self.SplineEditor is not None:return self.SplineEditor.HandleEvent(event)
        if event.type() in (QEvent.Type.Hide,QEvent.Type.FocusOut):self.FinishStroke()
        if event.type()==QEvent.Type.MouseButtonPress and event.button()==Qt.MouseButton.LeftButton and self._Target():
            if self.Runtime.IsPlaying():return False
            self.Controller.History.Begin(self.Tr('terrain.edit'));self.Stroke=True;self._point=event.position().toPoint();self._modifiers=event.modifiers();self._last=0;self._Apply(self._point,self._modifiers);self._stroke_timer.start();return True
        if event.type()==QEvent.Type.MouseMove:
            self._point=event.position().toPoint();self._modifiers=event.modifiers()
            if self.Stroke:return True
            if monotonic()-self._last>.05 and self._Target():
                self._last=monotonic()
                try:self.Runtime.SetToolGuides(self.Runtime.TerrainCommand(self._Target(),'hit',self._Values(self._point)).get('guides',[]))
                except (RuntimeError,ValueError):self.Runtime.SetToolGuides([])
        if event.type()==QEvent.Type.Leave:self.Runtime.SetToolGuides([])
        if event.type()==QEvent.Type.MouseButtonRelease and event.button()==Qt.MouseButton.LeftButton and self.Stroke:self.FinishStroke();return True
        return False
    def MarkDirty(self,identity):
        scene=self.Runtime.EntityDetails(identity).get('scene_uuid','') if identity else ''
        self.Controller.SetDirty(True,[scene] if scene else None)
    def Command(self,command,**extra):
        identity=self._Target()
        if not identity:return
        self.FinishStroke()
        try:
            if self.Controller._Mutate(self.Tr('terrain.edit'),lambda:self.Runtime.TerrainCommand(identity,command,dict(self.State,**extra)).get('changed',False)):self.MarkDirty(identity);self.Controller.SelectEntity(identity,force=True)
        except Exception as error:self.Window.Console.AddMessage(str(error),source='Terrain')
    def OpenSettings(self):
        if self.Dialog:self.Dialog.show();self.Dialog.raise_();return
        dialog=self.Dialog=QDialog(self.Window);dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose);dialog.setWindowTitle(self.Tr('terrain.settings'));dialog.setWindowModality(Qt.WindowModality.NonModal);dialog.resize(390,410);layout=QVBoxLayout(dialog);tabs=TabInput();layout.addWidget(tabs)
        def section(key):body=QWidget();form=QFormLayout(body);tabs.AddSection(self.Tr('terrain.'+key),body);return form
        brush=section('brush')
        for key,lo,hi in (('radius',.01,10000),('strength',0,10000),('falloff',0,1),('height',-100000,100000),('scale',-100000,100000)):
            field=FloatInput(minimum=lo,maximum=hi,value=self.State[key]);field.valueChanged.connect(lambda value,k=key:(self.State.update({k:value}),self._Save()));brush.addRow(self.Tr('terrain.'+key),field)
        picker=self.Controller._AssetPicker(self.State['stamp'],{'.png','.jpg','.jpeg','.bmp','.tga','.hdr'},lambda value:(self.State.update(stamp=value or ZERO),self._Save()));brush.addRow(self.Tr('terrain.stamp'),picker)
        for key in ('layer','prototype','spline'):
            field=IntInput(0,{'layer':31,'prototype':63,'spline':127}[key],self.State[key]);field.valueChanged.connect(lambda value,k=key:(self.State.update({k:value}),self._Save()));brush.addRow(self.Tr('terrain.'+key),field)
        actions=section('tools')
        for label,command in (('import_height','import_height'),('import_splat','import_splat'),('generate','generate'),('apply_spline','spline')):
            button=QPushButton(self.Tr('terrain.'+label));button.clicked.connect(lambda _,c=command:self.Command(c));actions.addRow(button)
        source=QComboBox()
        for index in range(self.Target.count()):
            if self.Target.itemData(index)!=self._Target():source.addItem(self.Target.itemText(index),self.Target.itemData(index))
        actions.addRow(self.Tr('terrain.boolean_source'),source)
        for operation,key in enumerate(('union','intersection','subtract')):
            button=QPushButton(self.Tr('terrain.'+key));button.setEnabled(source.count()>0);button.clicked.connect(lambda _,op=operation:self.Command('boolean',source=source.currentData(),operation=op));actions.addRow(button)
        edge=QComboBox();edge.addItems([self.Tr('terrain.edge_'+name) for name in ('left','right','front','back')]);actions.addRow(self.Tr('terrain.edge'),edge)
        match=QPushButton(self.Tr('terrain.match_edge'));match.setEnabled(source.count()>0);match.clicked.connect(lambda:self.Command('match_edge',source=source.currentData(),edge=edge.currentIndex()));actions.addRow(match)
        dialog.destroyed.connect(lambda *_:setattr(self,'Dialog',None));dialog.show()
