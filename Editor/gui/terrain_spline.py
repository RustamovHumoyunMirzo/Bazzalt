"""Interactive terrain spline anchors, Bézier handles and scene-local controls."""
from copy import deepcopy
from math import tan, radians

from PySide6.QtCore import QEvent, QPoint, Qt, QSignalBlocker
from PySide6.QtWidgets import QWidget, QFrame, QScrollArea, QVBoxLayout, QFormLayout, QHBoxLayout, QLabel, QPushButton, QComboBox, QCheckBox

from .gizmos import Vec3, GizmoMode, GizmoDrag, GizmoHandle, Plane, RayPlaneIntersection
from .widgets.fields import Vec3Input


class TerrainSplineEditor:
    def __init__(self, owner):
        self.Owner=owner
        self.Controller=owner.Controller
        self.Runtime=owner.Runtime
        self.Surface=owner.Surface
        self.Tr=owner.Tr
        self.Selected=-1
        self.Part=0
        self.Creating=True
        self.ControlsVisible=True
        self.Data={}
        self.Preview=None
        self._press=None
        self._before=None
        self._camera=None
        self._owner_id=''
        self.Overlay=QFrame(self.Surface)
        self.Overlay.setObjectName('TerrainSplineOverlay')
        outer=QVBoxLayout(self.Overlay)
        outer.setContentsMargins(1,1,1,1)
        scroll=QScrollArea(self.Overlay)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidgetResizable(True)
        self.Content=QWidget(scroll)
        scroll.setWidget(self.Content)
        outer.addWidget(scroll)
        layout=QVBoxLayout(self.Content)
        layout.setContentsMargins(8,8,8,8)
        layout.setSpacing(6)
        self.Title=QLabel(self.Tr('terrain.spline_edit'))
        layout.addWidget(self.Title)
        self.Splines=QComboBox()
        self.Splines.currentIndexChanged.connect(self._ChooseSpline)
        layout.addWidget(self.Splines)
        row=QHBoxLayout()
        self.New=QPushButton(self.Tr('terrain.new_spline'))
        self.New.clicked.connect(self.NewSpline)
        self.Continue=QPushButton(self.Tr('terrain.continue_spline'))
        self.Continue.clicked.connect(self.ContinueSpline)
        row.addWidget(self.New)
        row.addWidget(self.Continue)
        layout.addLayout(row)
        self.PointLabel=QLabel()
        layout.addWidget(self.PointLabel)
        form=self.Form=QFormLayout()
        form.setContentsMargins(0,0,0,0)
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapLongRows)
        self.Position=Vec3Input((0,0,0))
        self.Position.ValueChanged.connect(self._PositionChanged)
        form.addRow(self.Tr('field.Position'),self.Position)
        self.Shape=QComboBox()
        self.Shape.addItems((self.Tr('terrain.linear'),self.Tr('terrain.curved')))
        self.Shape.currentIndexChanged.connect(self._ShapeChanged)
        form.addRow(self.Tr('terrain.point_shape'),self.Shape)
        layout.addLayout(form)
        self.Linked=QCheckBox(self.Tr('terrain.link_handles'))
        self.Linked.setChecked(True)
        layout.addWidget(self.Linked)
        self.Closed=QCheckBox(self.Tr('terrain.closed_spline'))
        self.Closed.toggled.connect(lambda value:self.Edit('spline_close',closed=value))
        layout.addWidget(self.Closed)
        row=QHBoxLayout()
        self.Remove=QPushButton(self.Tr('terrain.remove_point'))
        self.Remove.clicked.connect(self.RemovePoint)
        self.Finish=QPushButton(self.Tr('terrain.finish_spline'))
        self.Finish.clicked.connect(self.FinishPath)
        row.addWidget(self.Remove)
        row.addWidget(self.Finish)
        layout.addLayout(row)
        self.Overlay.resize(260,320)
        self.Overlay.hide()
        self.Owner.ApplyTheme()
        self.Retranslate()

    def Retranslate(self):
        for widget,key in ((self.Title,'spline_edit'),(self.New,'new_spline'),(self.Continue,'continue_spline'),(self.Linked,'link_handles'),(self.Closed,'closed_spline'),(self.Remove,'remove_point'),(self.Finish,'finish_spline')):
            text=self.Tr('terrain.'+key)
            if widget in (self.New,self.Continue,self.Remove,self.Finish):
                widget.setText('');widget.setToolTip(text);widget.setAccessibleName(text)
            else:widget.setText(text)
        self.Form.labelForField(self.Position).setText(self.Tr('field.Position'))
        self.Form.labelForField(self.Shape).setText(self.Tr('terrain.point_shape'))
        self.Shape.setItemText(0,self.Tr('terrain.linear'))
        self.Shape.setItemText(1,self.Tr('terrain.curved'))
        self._RefreshFields()

    def IsActive(self):
        return self.Owner.Active and self.Owner.Tool==9 and not self.Runtime.IsPlaying()

    def HasSelection(self):
        return self.IsActive() and self.Controller.SelectedEntity==self.Owner._Target() and not self.Creating and 0<=self.Selected<len(self.Data.get('points',[]))

    def Activate(self):
        self._gizmo_signature=None
        self.Creating=True
        self.Refresh()
        if self.Surface._attached and not self.Surface._captured:
            self.Overlay.setAttribute(Qt.WidgetAttribute.WA_DontCreateNativeAncestors)
            self.Overlay.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
        self.Place()
        self.Overlay.setVisible(self.ControlsVisible)
        self.Overlay.raise_()
        self.Controller._UpdateGizmo()

    def ToggleControls(self):
        self.ControlsVisible=not self.ControlsVisible
        self.Overlay.setVisible(self.ControlsVisible and self.IsActive())
        if self.ControlsVisible:self.Place();self.Overlay.raise_()
        self.Surface.setFocus()

    def Deactivate(self):
        if self.Surface._grab_active and self.HasSelection():self.Surface.CancelGrab()
        self.Selected=-1
        self.Preview=None
        self._press=None
        self._gizmo_signature=None
        self.Overlay.hide()
        self.Runtime.SetToolGuides([])
        self.Surface.SetGizmoMode(self.Owner.Window.Toolbar.GetGizmoMode())
        self.Controller._UpdateGizmo()

    def Place(self):
        parent=self.Owner.Overlay
        if parent is None:return
        width=min(260,max(100,self.Surface.width()-16))
        height=max(280,self.Content.sizeHint().height()+2)
        self.Overlay.resize(width,min(height,max(60,self.Surface.height()-16)))
        x=parent.x()+parent.width()+8
        if x+width>self.Surface.width()-8:x=parent.x()-width-8
        x=max(0,min(self.Surface.width()-width-8,x))
        y=max(0,min(self.Surface.height()-self.Overlay.height()-8,parent.y()))
        self.Overlay.move(x,y)

    def Command(self, command, **values):
        identity=self.Owner._Target()
        if not identity:return {}
        try:
            return self.Runtime.TerrainCommand(identity,command,dict(spline=self.Owner.State['spline'],**values))
        except (RuntimeError,ValueError,IndexError) as error:
            self.Owner.Window.Console.AddMessage(str(error),source='Terrain')
            return {}

    def Refresh(self, *_):
        identity=self.Owner._Target()
        if identity!=self._owner_id:
            self.Selected=-1
            self._owner_id=identity
        rows=self.Runtime.EntityDetails(identity).get('component_data',{}).get('Terrain',{}).get('Splines',[]) if identity else []
        self.Owner.State['spline']=min(self.Owner.State['spline'],max(0,len(rows)-1))
        self.Data=self.Command('spline_info') if identity else {}
        blocker=QSignalBlocker(self.Splines)
        self.Splines.clear()
        for index,row in enumerate(rows):
            name=row.get('Name') or self.Tr('terrain.spline')
            self.Splines.addItem(f'{index+1} · {name}',index)
        self.Splines.setCurrentIndex(self.Splines.findData(self.Owner.State['spline']))
        del blocker
        if self.Selected>=len(self.Data.get('points',[])):self.Selected=-1
        self._RefreshFields()
        self.Draw()

    def _RefreshFields(self):
        valid=0<=self.Selected<len(self.Data.get('points',[]))
        self.PointLabel.setText(self.Tr('terrain.point_selected',index=self.Selected+1) if valid else self.Tr('terrain.place_points'))
        for widget in (self.Position,self.Shape,self.Linked,self.Remove):widget.setEnabled(valid)
        blocker=QSignalBlocker(self.Position)
        if valid:self.Position.SetValue(self.Data['points'][self.Selected])
        del blocker
        blocker=QSignalBlocker(self.Shape)
        if valid:
            anchor=Vec3(*self.Data['points'][self.Selected])
            curved=any((Vec3(*self.Data[key][self.Selected])-anchor).Length()>1.e-5 for key in ('incoming','outgoing'))
            self.Shape.setCurrentIndex(1 if curved else 0)
        del blocker
        blocker=QSignalBlocker(self.Closed)
        self.Closed.setChecked(self.Data.get('closed',False))
        del blocker
        self.Continue.setEnabled(bool(self.Data.get('points')) and not self.Data.get('closed',False))

    def _ChooseSpline(self, _):
        index=self.Splines.currentData()
        if index is None:return
        self.Owner.State['spline']=index
        self.Owner._Save()
        self.Selected=-1
        self.Creating=False
        self.Refresh()
        self.Controller._UpdateGizmo()

    def Edit(self, command, **values):
        if not self.IsActive():return False
        if self.Controller._Mutate(self.Tr('terrain.edit_spline'),lambda:bool(self.Command(command,**values).get('changed'))):
            self.Owner.MarkDirty(self.Owner._Target())
            self.Refresh()
            self.Controller._UpdateGizmo()
            return True
        return False

    def NewSpline(self):
        result={}
        def create():
            result.update(self.Command('spline_new'))
            return bool(result.get('changed'))
        if self.Controller._Mutate(self.Tr('terrain.new_spline'),create):
            self.Owner.State['spline']=result['spline']
            self.Selected=-1
            self.Creating=True
            self.Owner.MarkDirty(self.Owner._Target())
            self.Refresh()
            self.Controller._UpdateGizmo()

    def ContinueSpline(self):
        self.Creating=True
        self.Preview=None
        self.Controller._UpdateGizmo()
        self.Draw()
        self.Surface.setFocus()

    def FinishPath(self):
        self.Creating=False
        self.Preview=None
        self.Draw()
        self.Controller._UpdateGizmo()

    def RemovePoint(self):
        if self.Selected>=0:self.Edit('spline_remove',point=self.Selected)

    def _ShapeChanged(self, index):
        if self.Selected>=0:self.Edit('spline_mode',point=self.Selected,curved=index==1)

    def _PositionChanged(self, value):
        if self.Selected>=0:self.Edit('spline_move',point=self.Selected,part=0,position=value)

    def Select(self, point, part=0):
        self.Selected=point
        self.Part=part
        self.Creating=False
        self.Preview=None
        if self.Controller.SelectedEntity!=self.Owner._Target():self.Controller.SelectEntity(self.Owner._Target())
        self._RefreshFields()
        self.Controller._UpdateGizmo()
        self.Draw()

    def SelectedPosition(self):
        key=('points','incoming','outgoing')[self.Part]
        return self.Data[key][self.Selected]

    def UpdateGizmo(self):
        if not self.IsActive() or self.Controller.SelectedEntity!=self.Owner._Target():return False
        position=self.SelectedPosition() if self.HasSelection() else None
        signature=(self.Owner._Target(),position)
        if signature==getattr(self,'_gizmo_signature',None):return True
        self._gizmo_signature=signature
        self.Controller._gizmo_signature=None
        if position is not None:
            self.Surface.SetGizmoMode(GizmoMode.Translate)
            self.Surface.SetSelection(position)
            self.Runtime.SetGizmoPosition(position,1)
        else:
            self.Surface.SetSelection(None)
            self.Runtime.SetGizmo('',0)
        return True

    def BeginMove(self):
        self._before=deepcopy(self.Runtime.EntityDetails(self.Owner._Target())['component_data']['Terrain']['Splines'])
        self.Controller.History.Begin(self.Tr('terrain.edit_spline'))

    def Translate(self, delta):
        if not self.HasSelection():return False
        position=Vec3(*self.SelectedPosition())+delta
        result=self.Command('spline_move',point=self.Selected,part=self.Part,position=(position.X,position.Y,position.Z),linked=self.Linked.isChecked())
        if not result.get('changed'):return False
        self.Data=self.Command('spline_info')
        self.Owner.MarkDirty(self.Owner._Target())
        self.UpdateGizmo()
        self.Draw()
        return True

    def FinishMove(self):
        self._before=None
        self.Refresh()

    def CancelMove(self):
        if self._before is not None:
            self.Runtime.SetComponentProperty(self.Owner._Target(),'Terrain','Splines',self._before)
        self._before=None
        self.Refresh()

    def _Hit(self, position):
        candidates=[]
        if self.Selected>=0:
            anchor=Vec3(*self.Data['points'][self.Selected])
            for part,key in ((1,'incoming'),(2,'outgoing')):
                point=self.Data[key][self.Selected]
                if (Vec3(*point)-anchor).Length()>1.e-5:candidates.append((self.Selected,part,point))
        candidates.extend((index,0,point) for index,point in enumerate(self.Data.get('points',[])))
        hits=[]
        for index,part,point in candidates:
            projected=self.Surface._Project(point)
            if projected is None:continue
            distance=(projected[0]-position.x())**2+(projected[1]-position.y())**2
            if distance<=81:hits.append((distance,projected[2],index,part))
        if not hits:return None
        _,_,index,part=min(hits)
        return index,part

    def _Probe(self, position):
        result=self.Command('spline_probe',**{key:value for key,value in self.Owner._Values(position).items() if key!='spline'})
        if result.get('position') is not None:return result['position']
        points=self.Data.get('points',[])
        if not points:return None
        normal=(Vec3(*self.Surface._target)-Vec3(*self.Surface._eye)).Normalized()
        point=RayPlaneIntersection(self.Surface._Ray(position),Plane(Vec3(*points[-1]),normal))
        return (point.X,point.Y,point.Z) if point is not None else None

    def _HitPath(self, position):
        path=self.Data.get('path',[])
        for first,second in zip(path,path[1:]):
            a=self.Surface._Project(first)
            b=self.Surface._Project(second)
            if a is None or b is None:continue
            dx,dy=b[0]-a[0],b[1]-a[1]
            length=dx*dx+dy*dy
            if length<1.e-5:continue
            fraction=max(0.,min(1.,((position.x()-a[0])*dx+(position.y()-a[1])*dy)/length))
            if (position.x()-a[0]-fraction*dx)**2+(position.y()-a[1]-fraction*dy)**2<=36:return True
        return False

    def HandleEvent(self, event):
        if not self.IsActive():return False
        kind=event.type()
        if self.Surface._grab_active:return False
        if kind==QEvent.Type.KeyPress:
            if event.key()==Qt.Key.Key_Tab:
                self.ToggleControls()
                return True
            if event.key()==Qt.Key.Key_Escape:
                self.FinishPath()
                return True
            if event.key() in (Qt.Key.Key_Delete,Qt.Key.Key_Backspace):
                self.RemovePoint()
                return True
            if event.key()==Qt.Key.Key_G and self.Selected>=0:
                self.FinishPath()
                return False
        if kind==QEvent.Type.MouseButtonDblClick and event.button()==Qt.MouseButton.LeftButton:
            self.FinishPath()
            return True
        if kind==QEvent.Type.MouseButtonPress and event.button()==Qt.MouseButton.LeftButton:
            position=event.position().toPoint()
            self.Surface._scene_pointer=QPoint(position)
            self.Surface._has_scene_pointer=True
            hit=self._Hit(position)
            if hit is not None:
                self.Select(*hit)
                self._press=(QPoint(position),self.Surface._Ray(position))
                return True
            if self.Surface._PickGizmo(position) is not None:return False
            if not self.Creating and self._HitPath(position):
                self.Overlay.show()
                self.Overlay.raise_()
                return True
            values=self.Owner._Values(position)
            result={}
            def add():
                result.update(self.Command('spline_add',**{key:value for key,value in values.items() if key!='spline'}))
                return bool(result.get('changed'))
            if self.Controller._Mutate(self.Tr('terrain.add_point'),add):
                self.Selected=result['point']
                self.Part=0
                self.Creating=True
                self.Owner.MarkDirty(self.Owner._Target())
                self.Refresh()
                if self.Controller.SelectedEntity!=self.Owner._Target():self.Controller.SelectEntity(self.Owner._Target())
                self.Controller._UpdateGizmo()
            return True
        if kind==QEvent.Type.MouseMove:
            position=event.position().toPoint()
            if self._press is not None and event.buttons()&Qt.MouseButton.LeftButton:
                start,ray=self._press
                if (position-start).manhattanLength()>3 and self.Surface._gizmo_drag is None:
                    normal=(Vec3(*self.Surface._target)-Vec3(*self.Surface._eye)).Normalized()
                    self.Surface._StartTransformDrag(GizmoDrag(GizmoMode.Translate,GizmoHandle.Screen,ray,Vec3(*self.SelectedPosition()),normal))
                return self.Surface._gizmo_drag is None
            if self.Surface._gizmo_drag is not None:return False
            self.Preview=self._Probe(position) if self.Creating else None
            self.Draw()
            return False
        if kind==QEvent.Type.MouseButtonRelease and event.button()==Qt.MouseButton.LeftButton:
            self._press=None
            return self.Surface._gizmo_drag is None
        return False

    def Tick(self):
        if not self.IsActive():return
        camera=(tuple(self.Surface._eye),tuple(self.Surface._target),self.Surface.size().width(),self.Surface.size().height())
        if camera!=self._camera:
            self._camera=camera
            self.Draw()

    def Draw(self):
        if not self.IsActive():return
        lines=[]
        eye=Vec3(*self.Surface._eye)
        forward=(Vec3(*self.Surface._target)-eye).Normalized()
        right=forward.Cross(Vec3(0,1,0)).Normalized()
        up=right.Cross(forward).Normalized()
        def line(first,second,color):
            lines.append((*first,*second,*color,1.))
        def marker(position,size,color):
            center=Vec3(*position)
            depth=max(.01,(center-eye).Dot(forward))
            radius=depth*2*tan(radians(30))*size/max(1,self.Surface.height())
            corners=[center+right*radius,center+up*radius,center-right*radius,center-up*radius]
            for index in range(4):
                a,b=corners[index],corners[(index+1)%4]
                line((a.X,a.Y,a.Z),(b.X,b.Y,b.Z),color)
        path=self.Data.get('path',[])
        for first,second in zip(path,path[1:]):line(first,second,(.3,.75,1.))
        points=self.Data.get('points',[])
        for index,point in enumerate(points):
            if self.Surface._Project(point) is not None:marker(point,5,(1.,.75,.2) if index==self.Selected else (.4,.8,1.))
        if 0<=self.Selected<len(points):
            anchor=points[self.Selected]
            for key in ('incoming','outgoing'):
                handle=self.Data[key][self.Selected]
                if (Vec3(*handle)-Vec3(*anchor)).Length()>1.e-5:
                    line(anchor,handle,(.55,.7,.8))
                    marker(handle,4,(.8,.9,1.))
        if self.Preview is not None and points and not self.Data.get('closed',False):
            start=Vec3(*points[-1])
            handle=Vec3(*self.Data['outgoing'][-1])
            end=Vec3(*self.Preview)
            previous=start
            for sample in range(1,17):
                t=sample/16
                u=1-t
                point=start*(u*u*u)+handle*(3*u*u*t)+end*(3*u*t*t+t*t*t)
                line((previous.X,previous.Y,previous.Z),(point.X,point.Y,point.Z),(.8,.9,1.))
                previous=point
            marker(self.Preview,4,(.8,.9,1.))
        self.Runtime.SetToolGuides(lines)
