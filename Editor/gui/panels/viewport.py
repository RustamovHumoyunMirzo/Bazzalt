"""Qt native surfaces hosted by the private Filament editor bridge."""

from __future__ import annotations

from math import cos, radians, sin, tan

from PySide6.QtCore import QPoint, QPointF, QRect, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QKeyEvent, QMouseEvent, QPainter, QPen, QWheelEvent
from PySide6.QtWidgets import (QCheckBox, QComboBox, QFrame, QHBoxLayout, QLabel,
                               QStackedLayout, QToolButton,QVBoxLayout, QWidget)
from ..gizmos import GizmoDrag, GizmoHandle, GizmoMode, PickAxis, PickRotationAxis, Ray, Vec3


class SelectionMarquee(QWidget):
    """Transient screen-space overlay that remains visible above a native swap chain."""
    def __init__(self) -> None:
        flags=(Qt.WindowType.ToolTip|Qt.WindowType.FramelessWindowHint|
               Qt.WindowType.WindowDoesNotAcceptFocus|Qt.WindowType.WindowTransparentForInput)
        super().__init__(None,flags);self.setObjectName("SceneSelectionMarquee")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)

    def paintEvent(self,event)->None:  # type: ignore[no-untyped-def]
        painter=QPainter(self);painter.setRenderHint(QPainter.RenderHint.Antialiasing,False)
        painter.fillRect(self.rect(),QColor(55,135,235,42));painter.setPen(QPen(QColor(110,185,255,235),1));painter.drawRect(self.rect().adjusted(0,0,-1,-1));painter.end()


class NativeRenderSurface(QWidget):
    TranslationDragged = Signal(object)
    RotationDragged = Signal(object, float)
    ScaleDragged = Signal(object)
    GizmoDragFinished = Signal()
    CameraChanged = Signal(float, float)
    Attached = Signal()
    EntityPicked = Signal(str, bool)
    EntitiesBoxSelected = Signal(object, bool)
    SelectionBoxStarted = Signal(bool)
    SelectionBoxFinished = Signal()
    GizmoDragStarted = Signal()
    NavigationChanged = Signal(bool)
    _next_id = 1

    def __init__(self, runtime, scene: bool, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.Runtime = runtime; self.IsScene = scene
        self.ViewportId = NativeRenderSurface._next_id; NativeRenderSurface._next_id += 1
        self._attached = False; self._last = QPoint(); self._yaw = 36.0; self._pitch = 20.0
        self._distance = 12.0; self._target = [0.0, 0.0, 0.0]
        self._eye = (6.0,4.0,8.0); self._selection = None; self._mode = GizmoMode.Select
        self._gizmo_drag = None; self._hover_handle = None; self._last_delta = Vec3(); self._last_angle=0.0;self._last_scale=Vec3(1,1,1)
        self._orientation_animation=None
        self._selection_box_start=None
        self._selection_box_additive=False;self._selection_band=None
        self._selection_box_preview=()
        self._selection_box_dragging=False;self._pending_pick=""
        self._selected_entity_ids=set()
        self._navigating=False;self._fly_navigation=False;self._keys=set();self._move_speed=5.0
        self._look_sensitivity=.35;self._fly_boost=3.0
        self._fly_timer=QTimer(self);self._fly_timer.setInterval(16);self._fly_timer.timeout.connect(self._FlyTick);self._fly_timer.start()
        self.setAttribute(Qt.WidgetAttribute.WA_DontCreateNativeAncestors)
        self.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
        # A native render child can retain the OS cursor last used over a Qt
        # text editor unless it owns an explicit cursor. Gizmos never replace it.
        self.setCursor(Qt.CursorShape.ArrowCursor)

    def paintEngine(self):  # Filament owns every pixel on this native child surface.
        return None

    def showEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        super().showEvent(event); QTimer.singleShot(50, self._Attach)

    def _PixelSize(self) -> tuple[int, int]:
        ratio = self.devicePixelRatioF()
        return max(1, round(self.width() * ratio)), max(1, round(self.height() * ratio))

    def _Attach(self) -> None:
        if self._attached or not self.isVisible() or not self.Runtime.IsAvailable(): return
        # Delay native-window promotion until docking has finished constructing
        # and reparenting its tab hierarchy.
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
        self.setCursor(Qt.CursorShape.ArrowCursor)
        width, height = self._PixelSize()
        self._attached = self.Runtime.CreateViewport(
            self.ViewportId, int(self.winId()), self.IsScene, width, height,self.devicePixelRatioF())
        if self._attached and self.IsScene: self._UpdateCamera()
        if self._attached:self.Attached.emit()

    def resizeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        super().resizeEvent(event)
        if self._attached:
            width, height = self._PixelSize(); self.Runtime.ResizeViewport(self.ViewportId, width, height,self.devicePixelRatioF())

    def Detach(self) -> None:
        if self._attached: self.Runtime.DestroyViewport(self.ViewportId); self._attached = False

    def _UpdateCamera(self) -> None:
        yaw, pitch = radians(self._yaw), radians(self._pitch)
        cp = cos(pitch)
        eye = (self._target[0] + self._distance * cp * sin(yaw),
               self._target[1] + self._distance * sin(pitch),
               self._target[2] + self._distance * cp * cos(yaw))
        self._eye=eye;self.Runtime.SetSceneCamera(self.ViewportId, eye, tuple(self._target));self.CameraChanged.emit(self._yaw,self._pitch)

    def _LookFromEye(self) -> None:
        """Rotate the view direction without orbiting the camera position."""
        yaw,pitch=radians(self._yaw),radians(self._pitch);cp=cos(pitch)
        radial=(cp*sin(yaw),sin(pitch),cp*cos(yaw))
        self._target=[self._eye[0]-self._distance*radial[0],
                      self._eye[1]-self._distance*radial[1],
                      self._eye[2]-self._distance*radial[2]]
        self.Runtime.SetSceneCamera(self.ViewportId,self._eye,tuple(self._target));self.CameraChanged.emit(self._yaw,self._pitch)

    def _PanCamera(self,delta:QPoint)->None:
        """Translate the scene camera parallel to its current view plane."""
        eye=Vec3(*self._eye);forward=(Vec3(*self._target)-eye).Normalized()
        right=forward.Cross(Vec3(0,1,0)).Normalized();up=right.Cross(forward).Normalized()
        scale=max(.0001,self._distance*.0015)
        step=right*(-delta.x()*scale)+up*(delta.y()*scale)
        self._target[0]+=step.X;self._target[1]+=step.Y;self._target[2]+=step.Z
        self._UpdateCamera()

    def SetSelection(self, position) -> None:
        self._selection = Vec3(*position) if position is not None else None
        if position is None:self._SetHover(None)
    def SetGizmoMode(self, mode: GizmoMode) -> None:
        self._mode = mode; self._SetHover(None)
    def SetMoveSpeed(self,speed:float)->None:self._move_speed=max(.1,float(speed))
    def SetCameraPreset(self,preset:str)->None:
        values={"perspective":(36.,20.),"top":(0.,89.),"bottom":(0.,-89.),"left":(-90.,0.),"right":(90.,0.),"front":(0.,0.),"back":(180.,0.)}
        if preset in values:self._yaw,self._pitch=values[preset];self._UpdateCamera()
    def Frame(self,center,radius:float=2.0)->None:
        self._target=[float(value) for value in center];self._distance=max(1.0,float(radius)*2.2);self._UpdateCamera()

    def _DestroySelectionBand(self)->None:
        if self._selection_band is not None:self._selection_band.hide();self._selection_band.deleteLater();self._selection_band=None

    def _EntitiesInSelectionBox(self,start:QPoint,end:QPoint)->list[str]:
        left,right=sorted((start.x(),end.x()));top,bottom=sorted((start.y(),end.y()))
        if right-left<=4 and bottom-top<=4:return []
        selected=[]
        for entity in self.Runtime.Entities():
            projected=self._Project(entity.get("world_position",entity.get("position",(0,0,0))))
            if not projected:continue
            radius=0.0;entity_id=str(entity.get("uuid","") or "")
            details=self.Runtime.EntityDetails(entity_id) if entity_id else {};primitive=details.get("component_data",{}).get("Primitive Object")
            if primitive:
                scale=details.get("scale",(1,1,1));shape=int(primitive.get("Shape",0))
                if shape==0:size=primitive.get("Size",(1,1,1));world_radius=.5*sum((float(size[i])*abs(float(scale[i])))**2 for i in range(3))**.5
                elif shape==4:world_radius=.5*sum((float(primitive.get(name,1))*abs(float(scale[index])))**2 for name,index in (("Width",0),("Depth",2)))**.5
                elif shape==6:world_radius=(float(primitive.get("Major Radius",.75))+float(primitive.get("Minor Radius",.25)))*max(abs(float(v)) for v in scale)
                else:world_radius=max(float(primitive.get("Radius",.5))*max(abs(float(scale[0])),abs(float(scale[2]))),float(primitive.get("Height",1))*.5*abs(float(scale[1])))
                radius=world_radius*self.height()/(2.0*tan(radians(30.0))*projected[2])
            if projected[0]+radius>=left and projected[0]-radius<=right and projected[1]+radius>=top and projected[1]-radius<=bottom:selected.append(entity_id)
        return selected

    def _PreviewSelectionBox(self,current:QPoint)->list[str]:
        selected=self._EntitiesInSelectionBox(self._selection_box_start,current) if self._selection_box_start is not None else []
        signature=tuple(selected)
        if signature!=self._selection_box_preview:
            self._selection_box_preview=signature;self.EntitiesBoxSelected.emit(selected,self._selection_box_additive)
        return selected

    def _Ray(self, point: QPoint) -> Ray:
        eye=Vec3(*self._eye);target=Vec3(*self._target);forward=(target-eye).Normalized()
        right=forward.Cross(Vec3(0,1,0)).Normalized();up=right.Cross(forward).Normalized()
        x=2.0*point.x()/max(1,self.width())-1.0;y=1.0-2.0*point.y()/max(1,self.height())
        spread=tan(radians(30.0));aspect=self.width()/max(1,self.height())
        return Ray(eye,(forward+right*(x*spread*aspect)+up*(y*spread)).Normalized())

    def _Project(self,position)->tuple[float,float,float]|None:
        eye=Vec3(*self._eye);forward=(Vec3(*self._target)-eye).Normalized();right=forward.Cross(Vec3(0,1,0)).Normalized();up=right.Cross(forward).Normalized();rel=Vec3(*position)-eye;depth=rel.Dot(forward)
        if depth<=.05:return None
        focal=self.height()/(2.0*tan(radians(30.0)))
        return self.width()*.5+rel.Dot(right)*focal/depth,self.height()*.5-rel.Dot(up)*focal/depth,depth

    def _PickSceneIcon(self,point:QPoint)->str:
        best="";best_distance=18.0*18.0;best_depth=float("inf")
        try:entities=self.Runtime.Entities()
        except Exception:return ""
        for entity in entities:
            components=entity.get("components",())
            if "Camera" not in components and "Light" not in components:continue
            projected=self._Project(entity.get("world_position",entity.get("position",(0,0,0))))
            if projected is None:continue
            x,y,depth=projected;distance=(x-point.x())**2+(y-point.y())**2
            if distance<=best_distance and depth<best_depth:best=str(entity.get("uuid",""));best_distance=distance;best_depth=depth
        return best

    def _PickSceneObject(self,point:QPoint)->str:
        ray=self._Ray(point)
        return self.Runtime.PickPrimitive((ray.Origin.X,ray.Origin.Y,ray.Origin.Z),(ray.Direction.X,ray.Direction.Y,ray.Direction.Z))

    def _PickGizmo(self, point: QPoint):
        if self._selection is None or self._mode is GizmoMode.Select:return None
        ray=self._Ray(point);depth=(Vec3(*self._eye)-self._selection).Length();world_per_pixel=depth*2.*tan(radians(30.))/max(1,self.height());length=world_per_pixel*96.;tolerance=world_per_pixel*11.
        if self._mode is GizmoMode.Rotate:return PickRotationAxis(ray,self._selection,length/.9,tolerance)
        # Match the visible arrow shafts in screen space; a ray near the common
        # origin must not turn ordinary object clicks into accidental drags.
        origin=self._Project((self._selection.X,self._selection.Y,self._selection.Z))
        if origin is None:return None
        candidates=[]
        for handle,axis in ((GizmoHandle.X,Vec3(1,0,0)),(GizmoHandle.Y,Vec3(0,1,0)),(GizmoHandle.Z,Vec3(0,0,1))):
            end=self._selection+axis*length;projected=self._Project((end.X,end.Y,end.Z))
            if projected is None:continue
            dx,dy=projected[0]-origin[0],projected[1]-origin[1];squared=dx*dx+dy*dy
            if squared<16:continue
            t=((point.x()-origin[0])*dx+(point.y()-origin[1])*dy)/squared
            if not .18<=t<=1.12:continue
            distance=((point.x()-origin[0]-t*dx)**2+(point.y()-origin[1]-t*dy)**2)**.5
            if distance<=7:candidates.append((distance,handle))
        return min(candidates,key=lambda value:value[0])[1] if candidates else None

    def _SetHover(self, handle) -> None:
        if handle is self._hover_handle:return
        self._hover_handle=handle
        axis={GizmoHandle.X:0,GizmoHandle.Y:1,GizmoHandle.Z:2}.get(handle,-1)
        self.Runtime.SetGizmoHover(axis)

    def _PickOrientation(self,point:QPoint)->bool:
        if point.x()<self.width()-88 or point.y()>88:return False
        cy,sy=cos(radians(self._yaw)),sin(radians(self._yaw));cp,sp=cos(radians(self._pitch)),sin(radians(self._pitch));center=QPointF(self.width()-44,44)
        def project(v):
            x,y,z=v;x,z=x*cy-z*sy,x*sy+z*cy;y,z=y*cp-z*sp,y*sp+z*cp
            return QPointF(center.x()+x*21,center.y()-y*21)
        axes=((project((1,0,0)),(90.,0.)),(project((0,1,0)),(self._yaw,89.)),(project((0,0,1)),(0.,0.)))
        position=QPointF(point)
        def line_distance(end):
            # Ignore the crowded center and hit-test the visible axis segment.
            start=center+(end-center)*.18;segment=end-start;length=segment.x()*segment.x()+segment.y()*segment.y()
            if length<=.0001:return (position-end).manhattanLength()
            offset=position-start;t=max(0.,min(1.,(offset.x()*segment.x()+offset.y()*segment.y())/length));nearest=start+segment*t
            dx=position.x()-nearest.x();dy=position.y()-nearest.y();return (dx*dx+dy*dy)**.5
        _end,target=min(axes,key=lambda item:line_distance(item[0]))
        if line_distance(_end)>9:return False
        start_yaw=self._yaw;delta=(target[0]-start_yaw+180)%360-180;start_pitch=self._pitch;step=0
        timer=QTimer(self);timer.setInterval(16)
        def tick():
            nonlocal step;step+=1;t=min(1.,step/12.);ease=1-(1-t)**3
            self._yaw=start_yaw+delta*ease;self._pitch=start_pitch+(target[1]-start_pitch)*ease;self._UpdateCamera()
            if t>=1:timer.stop()
        timer.timeout.connect(tick);self._orientation_animation=timer;timer.start();return True

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._last = event.position().toPoint(); self.setFocus()
        if self.IsScene:self.Runtime.SetObjectHover("",self._eye)
        if self.IsScene and event.button()==Qt.MouseButton.RightButton:self._keys.clear();self._fly_navigation=False;self._navigating=True;self.NavigationChanged.emit(True);event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and self._PickOrientation(self._last):event.accept();return
        picked=self._PickSceneObject(self._last) or self._PickSceneIcon(self._last) if self.IsScene and event.button()==Qt.MouseButton.LeftButton else ""
        different_object=bool(picked and picked not in self._selected_entity_ids)
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and not different_object and self._selection and self._mode is not GizmoMode.Select:
            ray=self._Ray(self._last);handle=self._PickGizmo(self._last)
            if handle is not None:
                self._SetHover(handle)
                self.GizmoDragStarted.emit()
                self._gizmo_drag=GizmoDrag(self._mode,handle,ray,self._selection,(Vec3(*self._target)-Vec3(*self._eye)).Normalized());self._last_delta=Vec3();self._last_angle=0.0;self._last_scale=Vec3(1,1,1);event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton:
            entity_id=picked
            self._selection_box_start=self._last;self._selection_box_additive=bool(event.modifiers()&Qt.KeyboardModifier.ControlModifier)
            self._selection_box_preview=();self._selection_box_dragging=False;self._pending_pick=entity_id
            event.accept();return

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if not self.IsScene: return
        current = event.position().toPoint(); delta = current - self._last; self._last = current
        if self._selection_box_start is not None and event.buttons()&Qt.MouseButton.LeftButton:
            distance=(current-self._selection_box_start).manhattanLength()
            if distance<=4 and not self._selection_box_dragging:return
            if not self._selection_box_dragging:
                self._selection_box_dragging=True;self._pending_pick="";self.SelectionBoxStarted.emit(self._selection_box_additive)
            if self._selection_band is None:self._selection_band=SelectionMarquee()
            start_global=self.mapToGlobal(self._selection_box_start);current_global=self.mapToGlobal(current);geometry=QRect(start_global,current_global).normalized()
            if geometry.width()>2 or geometry.height()>2:self._selection_band.setGeometry(geometry);self._selection_band.show();self._selection_band.raise_()
            else:self._selection_band.hide()
            self._PreviewSelectionBox(current)
            return
        if self._gizmo_drag is not None and event.buttons()&Qt.MouseButton.LeftButton:
            result=self._gizmo_drag.Calculate(self._Ray(current),translation_snap=.1,rotation_snap=radians(5),scale_snap=.05)
            if self._mode is GizmoMode.Translate:
                step=result.Translation-self._last_delta;self._last_delta=result.Translation;self.TranslationDragged.emit(step)
            elif self._mode is GizmoMode.Rotate:
                step=result.RotationRadians-self._last_angle;self._last_angle=result.RotationRadians;self.RotationDragged.emit(result.RotationAxis,step)
            else:
                prior=self._last_scale;value=result.Scale;step=Vec3(value.X/prior.X,value.Y/prior.Y,value.Z/prior.Z);self._last_scale=value;self.ScaleDragged.emit(step)
            return
        if event.buttons() & Qt.MouseButton.RightButton:
            movement_keys={Qt.Key.Key_W,Qt.Key.Key_A,Qt.Key.Key_S,Qt.Key.Key_D,Qt.Key.Key_Q,Qt.Key.Key_E}
            if event.modifiers()&Qt.KeyboardModifier.ShiftModifier and not self._keys.intersection(movement_keys):
                self._PanCamera(delta)
            else:
                self._yaw-=delta.x()*self._look_sensitivity;self._pitch=max(-89.0,min(89.0,self._pitch+delta.y()*self._look_sensitivity))
                # RMB alone is the usual editor orbit. Once fly keys are held,
                # preserve the eye and turn like a first-person camera.
                if self._fly_navigation and self._keys.intersection(movement_keys):self._LookFromEye()
                else:self._UpdateCamera()
        elif event.buttons() & Qt.MouseButton.MiddleButton:
            scale = self._distance * .0015
            self._target[0] += delta.x() * scale; self._target[1] -= delta.y() * scale; self._UpdateCamera()
        elif not event.buttons():
            handle=self._PickGizmo(current);self._SetHover(handle)
            self.Runtime.SetObjectHover(self._PickSceneObject(current) if handle is None else "",self._eye)

    def wheelEvent(self, event: QWheelEvent) -> None:
        if self.IsScene:
            self._distance = max(.1, min(5000.0, self._distance * (0.88 ** (event.angleDelta().y() / 120.0))))
            self._UpdateCamera(); event.accept()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button()==Qt.MouseButton.LeftButton:
            if self._selection_box_start is not None:
                start=self._selection_box_start;self._selection_box_start=None;end=event.position().toPoint();dragging=self._selection_box_dragging;pending=self._pending_pick
                self._selection_box_dragging=False;self._pending_pick=""
                self._DestroySelectionBand()
                if dragging:
                    selected=self._EntitiesInSelectionBox(start,end)
                    if tuple(selected)!=self._selection_box_preview:self.EntitiesBoxSelected.emit(selected,self._selection_box_additive)
                elif pending:
                    self.EntityPicked.emit(pending,self._selection_box_additive)
                else:self.SelectionBoxStarted.emit(False)
                self._selection_box_preview=();self.SelectionBoxFinished.emit();event.accept();return
            dragged=self._gizmo_drag is not None;self._gizmo_drag=None
            if dragged:self.GizmoDragFinished.emit()
            self._hover_handle=None;self._SetHover(self._PickGizmo(event.position().toPoint()))
        if event.button()==Qt.MouseButton.RightButton:self._navigating=False;self._fly_navigation=False;self._keys.clear();self.NavigationChanged.emit(False);event.accept()

    def keyPressEvent(self,event:QKeyEvent)->None:
        if self._navigating and event.key() in (Qt.Key.Key_W,Qt.Key.Key_A,Qt.Key.Key_S,Qt.Key.Key_D,Qt.Key.Key_Q,Qt.Key.Key_E,Qt.Key.Key_Shift):
            self._keys.add(event.key())
            if event.key()!=Qt.Key.Key_Shift:self._fly_navigation=True
            event.accept();return
        super().keyPressEvent(event)

    def keyReleaseEvent(self,event:QKeyEvent)->None:
        self._keys.discard(event.key())
        movement_keys={Qt.Key.Key_W,Qt.Key.Key_A,Qt.Key.Key_S,Qt.Key.Key_D,Qt.Key.Key_Q,Qt.Key.Key_E}
        if not self._keys.intersection(movement_keys):self._fly_navigation=False
        event.accept()

    def focusOutEvent(self,event)->None:
        self.Runtime.SetObjectHover("",self._eye)
        was_navigating=self._navigating;self._keys.clear();self._navigating=False;self._fly_navigation=False
        if was_navigating:self.NavigationChanged.emit(False)
        self._DestroySelectionBand()
        if self._selection_box_start is not None:self.SelectionBoxFinished.emit()
        self._selection_box_start=None;self._selection_box_preview=();super().focusOutEvent(event)
        self._selection_box_dragging=False;self._pending_pick=""

    def leaveEvent(self,event)->None:
        self.Runtime.SetObjectHover("",self._eye)
        if self._gizmo_drag is None:self._SetHover(None)
        super().leaveEvent(event)

    def enterEvent(self,event)->None:
        self.setCursor(Qt.CursorShape.ArrowCursor);super().enterEvent(event)

    def _FlyTick(self)->None:
        if not self._navigating or not self._keys:return
        eye=Vec3(*self._eye);forward=(Vec3(*self._target)-eye).Normalized();right=forward.Cross(Vec3(0,1,0)).Normalized();up=Vec3(0,1,0)
        direction=Vec3()
        if Qt.Key.Key_W in self._keys:direction+=forward
        if Qt.Key.Key_S in self._keys:direction-=forward
        if Qt.Key.Key_D in self._keys:direction+=right
        if Qt.Key.Key_A in self._keys:direction-=right
        if Qt.Key.Key_E in self._keys:direction+=up
        if Qt.Key.Key_Q in self._keys:direction-=up
        if direction.Dot(direction)<=0:return
        speed=self._move_speed*(self._fly_boost if Qt.Key.Key_Shift in self._keys else 1.0)*.016
        step=direction.Normalized()*speed;self._target[0]+=step.X;self._target[1]+=step.Y;self._target[2]+=step.Z;self._UpdateCamera()


class ViewportPanel(QFrame):
    def __init__(self, runtime, scene: bool, localization, resources=None, themes=None, parent=None) -> None:
        super().__init__(parent); self.setObjectName("ViewportPanel")
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(0)
        if scene:
            controls=QFrame();controls.setObjectName("SceneViewControls");row=QHBoxLayout(controls);row.setContentsMargins(7,3,7,3);row.setSpacing(6)
            self.LocalToggle=QToolButton();self.LocalToggle.setObjectName("SceneToolChip");self.LocalToggle.setCheckable(True);self.LocalToggle.setToolTip("Local transform space")
            self.PivotMode=QComboBox();self.PivotMode.setObjectName("ScenePivotMode");self.PivotMode.addItems(("Pivot","Center"));self.PivotMode.setToolTip("Gizmo pivot position")
            self.ShadingMode=QComboBox();self.ShadingMode.addItems(("Lit","Unlit","Wireframe","Lighting Only","Overdraw"))
            plane=self.GridPlane=QComboBox();plane.addItems(("XY","XZ","YZ"));plane.setCurrentIndex(1)
            grid=self.GridToggle=QToolButton();self.GizmoToggle=QToolButton();self.StatsToggle=QToolButton()
            for button,tooltip,checked in ((grid,"Grid",True),(self.GizmoToggle,"Gizmos",True),(self.StatsToggle,"Statistics",False)):
                button.setObjectName("SceneToolChip");button.setCheckable(True);button.setChecked(checked);button.setToolTip(tooltip)
            self.StatsLabel=QLabel();self.StatsLabel.setObjectName("SceneStats");self.StatsLabel.hide()
            for widget in (self.LocalToggle,self.PivotMode,self.ShadingMode,plane,grid,self.GizmoToggle,self.StatsToggle):row.addWidget(widget)
            row.addWidget(self.StatsLabel);row.addStretch();layout.addWidget(controls)
            def update_icons(_theme=None):
                theme="light" if themes and themes.GetTheme().background=="#d4d4d4" else "dark"
                if resources:
                    for button,name in ((self.LocalToggle,"localpos.svg"),(grid,"grid.svg"),(self.GizmoToggle,"gizmos.svg"),(self.StatsToggle,"stats.svg")):button.setIcon(resources.Icon(f"icons/stoolbar/{theme}/{name}"));button.setIconSize(QSize(16,16))
            update_icons();themes.ThemeChanged.connect(update_icons) if themes else None
        self.Surface = NativeRenderSurface(runtime, scene)
        if scene:
            layout.addWidget(self.Surface, 1)
        else:
            self._output_stack = QStackedLayout()
            self._output_stack.setContentsMargins(0, 0, 0, 0)
            self._output_stack.setStackingMode(QStackedLayout.StackingMode.StackOne)
            self._output_stack.addWidget(self.Surface)
            self._no_camera = QLabel()
            self._no_camera.setObjectName("NoGameCameraWarning")
            self._no_camera.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self._no_camera.setStyleSheet(
                "QLabel#NoGameCameraWarning { background: #000000; color: #999999; "
                "font-size: 13px; padding: 20px; }"
            )
            self._no_camera.setWordWrap(True)
            self._output_stack.addWidget(self._no_camera)
            layout.addLayout(self._output_stack, 1)
            self._localization = localization
            self._UpdateNoCameraText()
            localization.LocaleChanged.connect(lambda _locale: self._UpdateNoCameraText())
            self.SetGameCameraAvailable(False)
        if scene:
            grid.toggled.connect(lambda checked:runtime.SetGrid(checked,plane.currentIndex()))
            plane.currentIndexChanged.connect(lambda index:runtime.SetGrid(grid.isChecked(),index))
            self.StatsToggle.toggled.connect(self.StatsLabel.setVisible)
            self.Surface.SetMoveSpeed(5);runtime.SetGrid(True,1)

    def _UpdateNoCameraText(self) -> None:
        if hasattr(self, "_no_camera"):
            self._no_camera.setText(self._localization.Translate("viewport.no_game_camera"))

    def SetGameCameraAvailable(self, available: bool) -> None:
        if not hasattr(self, "_output_stack"): return
        self._output_stack.setCurrentWidget(self.Surface if available else self._no_camera)
        if available: QTimer.singleShot(0, self.Surface._Attach)

    def resizeEvent(self,event)->None:
        super().resizeEvent(event)

    def Detach(self) -> None:
        self.Surface.Detach()


__all__ = ["NativeRenderSurface", "ViewportPanel"]
