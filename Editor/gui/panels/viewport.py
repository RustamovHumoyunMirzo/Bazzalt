"""Qt native surfaces hosted by the private Filament editor bridge."""

from __future__ import annotations

from math import cos, radians, sin, tan

from PySide6.QtCore import QPoint, QPointF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QKeyEvent, QMouseEvent, QPainter, QPen, QPolygonF, QWheelEvent
from PySide6.QtWidgets import QCheckBox, QComboBox, QFrame, QHBoxLayout, QLabel, QVBoxLayout, QWidget
from ..gizmos import GizmoDrag, GizmoHandle, GizmoMode, PickAxis, PickRotationAxis, Ray, Vec3


class NativeRenderSurface(QWidget):
    TranslationDragged = Signal(object)
    RotationDragged = Signal(object, float)
    ScaleDragged = Signal(object)
    GizmoDragFinished = Signal()
    CameraChanged = Signal(float, float)
    Attached = Signal()
    EntityPicked = Signal(str)
    _next_id = 1

    def __init__(self, runtime, scene: bool, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.Runtime = runtime; self.IsScene = scene
        self.ViewportId = NativeRenderSurface._next_id; NativeRenderSurface._next_id += 1
        self._attached = False; self._last = QPoint(); self._yaw = 36.0; self._pitch = 20.0
        self._distance = 12.0; self._target = [0.0, 0.0, 0.0]
        self._eye = (6.0,4.0,8.0); self._selection = None; self._mode = GizmoMode.Select
        self._gizmo_drag = None; self._hover_handle = None; self._last_delta = Vec3(); self._last_angle=0.0;self._last_scale=Vec3(1,1,1)
        self._navigating=False;self._keys=set();self._move_speed=5.0
        self._fly_timer=QTimer(self);self._fly_timer.setInterval(16);self._fly_timer.timeout.connect(self._FlyTick);self._fly_timer.start()
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow)
        self.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)

    def paintEngine(self):  # Filament owns every pixel on this native child surface.
        return None

    def showEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        super().showEvent(event); QTimer.singleShot(0, self._Attach)

    def _PixelSize(self) -> tuple[int, int]:
        ratio = self.devicePixelRatioF()
        return max(1, round(self.width() * ratio)), max(1, round(self.height() * ratio))

    def _Attach(self) -> None:
        if self._attached or not self.isVisible() or not self.Runtime.IsAvailable(): return
        width, height = self._PixelSize()
        self._attached = self.Runtime.CreateViewport(
            self.ViewportId, int(self.winId()), self.IsScene, width, height)
        if self._attached and self.IsScene: self._UpdateCamera()
        if self._attached:self.Attached.emit()

    def resizeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        super().resizeEvent(event)
        if self._attached:
            width, height = self._PixelSize(); self.Runtime.ResizeViewport(self.ViewportId, width, height)

    def Detach(self) -> None:
        if self._attached: self.Runtime.DestroyViewport(self.ViewportId); self._attached = False

    def _UpdateCamera(self) -> None:
        yaw, pitch = radians(self._yaw), radians(self._pitch)
        cp = cos(pitch)
        eye = (self._target[0] + self._distance * cp * sin(yaw),
               self._target[1] + self._distance * sin(pitch),
               self._target[2] + self._distance * cp * cos(yaw))
        self._eye=eye;self.Runtime.SetSceneCamera(self.ViewportId, eye, tuple(self._target));self.CameraChanged.emit(self._yaw,self._pitch)

    def SetSelection(self, position) -> None:
        self._selection = Vec3(*position) if position is not None else None
        if position is None:self._SetHover(None)
    def SetGizmoMode(self, mode: GizmoMode) -> None:
        self._mode = mode; self._SetHover(None)
    def SetMoveSpeed(self,speed:float)->None:self._move_speed=max(.1,float(speed))

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

    def _PickGizmo(self, point: QPoint):
        if self._selection is None or self._mode is GizmoMode.Select:return None
        ray=self._Ray(point);depth=(Vec3(*self._eye)-self._selection).Length();world_per_pixel=depth*2.*tan(radians(30.))/max(1,self.height());length=world_per_pixel*96.;tolerance=world_per_pixel*11.
        if self._mode is GizmoMode.Rotate:return PickRotationAxis(ray,self._selection,length/.9,tolerance)
        return PickAxis(ray,self._selection,length,tolerance)

    def _SetHover(self, handle) -> None:
        if handle is self._hover_handle:return
        self._hover_handle=handle
        axis={GizmoHandle.X:0,GizmoHandle.Y:1,GizmoHandle.Z:2}.get(handle,-1)
        self.Runtime.SetGizmoHover(axis)
        if not self._navigating:self.setCursor(Qt.CursorShape.OpenHandCursor if handle else Qt.CursorShape.ArrowCursor)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._last = event.position().toPoint(); self.setFocus()
        if self.IsScene and event.button()==Qt.MouseButton.RightButton:self._navigating=True;event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and self._selection and self._mode is not GizmoMode.Select:
            ray=self._Ray(self._last);handle=self._PickGizmo(self._last)
            if handle is not None:
                self._SetHover(handle);self.setCursor(Qt.CursorShape.ClosedHandCursor)
                self._gizmo_drag=GizmoDrag(self._mode,handle,ray,self._selection,(Vec3(*self._target)-Vec3(*self._eye)).Normalized());self._last_delta=Vec3();self._last_angle=0.0;self._last_scale=Vec3(1,1,1);event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton:
            entity_id=self._PickSceneIcon(self._last)
            if entity_id:self.EntityPicked.emit(entity_id);event.accept();return

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if not self.IsScene: return
        current = event.position().toPoint(); delta = current - self._last; self._last = current
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
            self._yaw -= delta.x() * .35; self._pitch = max(-89.0, min(89.0, self._pitch + delta.y() * .35)); self._UpdateCamera()
        elif event.buttons() & Qt.MouseButton.MiddleButton:
            scale = self._distance * .0015
            self._target[0] += delta.x() * scale; self._target[1] -= delta.y() * scale; self._UpdateCamera()
        elif not event.buttons():self._SetHover(self._PickGizmo(current))

    def wheelEvent(self, event: QWheelEvent) -> None:
        if self.IsScene:
            self._distance = max(.1, min(5000.0, self._distance * (0.88 ** (event.angleDelta().y() / 120.0))))
            self._UpdateCamera(); event.accept()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button()==Qt.MouseButton.LeftButton:
            dragged=self._gizmo_drag is not None;self._gizmo_drag=None
            if dragged:self.GizmoDragFinished.emit()
            self._hover_handle=None;self._SetHover(self._PickGizmo(event.position().toPoint()))
        if event.button()==Qt.MouseButton.RightButton:self._navigating=False;self._keys.clear();event.accept()

    def keyPressEvent(self,event:QKeyEvent)->None:
        if self._navigating and event.key() in (Qt.Key.Key_W,Qt.Key.Key_A,Qt.Key.Key_S,Qt.Key.Key_D,Qt.Key.Key_Q,Qt.Key.Key_E,Qt.Key.Key_Shift):self._keys.add(event.key());event.accept();return
        super().keyPressEvent(event)

    def keyReleaseEvent(self,event:QKeyEvent)->None:
        self._keys.discard(event.key());event.accept()

    def focusOutEvent(self,event)->None:
        self._keys.clear();self._navigating=False;super().focusOutEvent(event)

    def leaveEvent(self,event)->None:
        if self._gizmo_drag is None:self._SetHover(None)
        super().leaveEvent(event)

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
        speed=self._move_speed*(3.0 if Qt.Key.Key_Shift in self._keys else 1.0)*.016
        step=direction.Normalized()*speed;self._target[0]+=step.X;self._target[1]+=step.Y;self._target[2]+=step.Z;self._UpdateCamera()


class SceneOrientationWidget(QWidget):
    def __init__(self,surface:NativeRenderSurface)->None:
        flags=Qt.WindowType.Tool|Qt.WindowType.FramelessWindowHint|Qt.WindowType.WindowDoesNotAcceptFocus
        super().__init__(surface.window(),flags);self.Surface=surface;self._yaw=36.;self._pitch=20.;self._axis_points=[];self._animation=None;self.setFixedSize(92,92);self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground);self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating);self.setAutoFillBackground(False);self.setCursor(Qt.CursorShape.PointingHandCursor)
        timer=QTimer(self);timer.setInterval(50);timer.timeout.connect(self._Sync);timer.start();self._timer=timer
    def _Sync(self)->None:
        if not self.Surface.isVisible() or self.Surface.window().isMinimized():self.hide();return
        point=self.Surface.mapToGlobal(QPoint(max(6,self.Surface.width()-98),6));self.move(point);self.show()
    def SetCamera(self,yaw:float,pitch:float)->None:self._yaw=yaw;self._pitch=pitch;self.update()
    def mousePressEvent(self,event:QMouseEvent)->None:
        if event.button()!=Qt.MouseButton.LeftButton:return
        point=event.position();nearest=min(self._axis_points,key=lambda item:(item[0]-point).manhattanLength(),default=None)
        if nearest is not None and (nearest[0]-point).manhattanLength()<=20:self._AnimateTo(*nearest[1])
    def _AnimateTo(self,yaw:float,pitch:float)->None:
        start_yaw=self.Surface._yaw;delta=(yaw-start_yaw+180)%360-180;start_pitch=self.Surface._pitch;step=0
        timer=QTimer(self);timer.setInterval(16)
        def tick():
            nonlocal step;step+=1;t=min(1.,step/12.);ease=1-(1-t)**3
            self.Surface._yaw=start_yaw+delta*ease;self.Surface._pitch=start_pitch+(pitch-start_pitch)*ease;self.Surface._UpdateCamera()
            if t>=1:timer.stop()
        timer.timeout.connect(tick);self._animation=timer;timer.start()
    def paintEvent(self,event)->None:
        p=QPainter(self);p.setRenderHint(QPainter.RenderHint.Antialiasing)
        cy,sy=cos(radians(self._yaw)),sin(radians(self._yaw));cp,sp=cos(radians(self._pitch)),sin(radians(self._pitch));center=QPointF(46,46)
        def project(v):
            x,y,z=v;x,z=x*cy-z*sy,x*sy+z*cy;y,z=y*cp-z*sp,y*sp+z*cp;return QPointF(center.x()+x*24,center.y()-y*24)
        corners=[project((x*.55,y*.55,z*.55)) for x,y,z in ((-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1))]
        p.setPen(QPen(QColor(225,225,230),1.2))
        for face,color in (((4,5,6,7),QColor(65,105,210,185)),((1,5,6,2),QColor(205,70,65,180)),((3,2,6,7),QColor(75,175,85,180))):
            p.setBrush(color);p.drawPolygon(QPolygonF([corners[i] for i in face]))
        for a,b in ((0,1),(1,2),(2,3),(3,0),(4,5),(5,6),(6,7),(7,4),(0,4),(1,5),(2,6),(3,7)):p.drawLine(corners[a],corners[b])
        axes=((project((1,0,0)),QColor('#d45b5b'),'X',(90.,0.)),(project((0,1,0)),QColor('#68a85c'),'Y',(self._yaw,89.)),(project((0,0,1)),QColor('#5686cf'),'Z',(0.,0.)));self._axis_points=[(end,target) for end,_color,_label,target in axes]
        for end,color,label,_target in axes:p.setPen(QPen(color,2));p.drawLine(center,end);p.setBrush(color);p.setPen(Qt.PenStyle.NoPen);p.drawEllipse(end,8,8);p.setPen(QColor('white'));p.drawText(end.x()-4,end.y()+4,label)
        p.end()


class SceneIconOverlay(QWidget):
    def __init__(self, surface: NativeRenderSurface, runtime, resources, parent=None)->None:
        flags=Qt.WindowType.Tool|Qt.WindowType.FramelessWindowHint|Qt.WindowType.WindowTransparentForInput|Qt.WindowType.WindowDoesNotAcceptFocus
        super().__init__(surface.window(),flags);self.Surface=surface;self.Runtime=runtime
        self.CameraIcon=resources.Pixmap("icons/scene_cam.svg") if resources else None
        self.LightIcon=resources.Pixmap("icons/scene_light.svg") if resources else None
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents);self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground);self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating);self.setAutoFillBackground(False)
        surface.CameraChanged.connect(lambda _yaw,_pitch:self.update());runtime.SceneChanged.connect(self.update)
        timer=QTimer(self);timer.setInterval(50);timer.timeout.connect(self._Sync);timer.start();self._timer=timer

    def _Sync(self)->None:
        if not self.Surface.isVisible() or self.Surface.window().isMinimized():self.hide();return
        self.setGeometry(self.Surface.mapToGlobal(QPoint(0,0)).x(),self.Surface.mapToGlobal(QPoint(0,0)).y(),self.Surface.width(),self.Surface.height());self.show();self.update()

    def paintEvent(self,event)->None:
        if self.width()<2 or self.height()<2:return
        try: entities=self.Runtime.Entities()
        except Exception:return
        eye=Vec3(*self.Surface._eye);forward=(Vec3(*self.Surface._target)-eye).Normalized();right=forward.Cross(Vec3(0,1,0)).Normalized();up=right.Cross(forward).Normalized()
        focal=self.height()/(2.0*tan(radians(30.0)));p=QPainter(self);p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        for entity in entities:
            components=entity.get("components",());icon=self.CameraIcon if "Camera" in components else self.LightIcon if "Light" in components else None
            if icon is None or icon.isNull():continue
            rel=Vec3(*entity.get("world_position",entity.get("position",(0,0,0))))-eye;depth=rel.Dot(forward)
            if depth<=.05:continue
            x=self.width()*.5+rel.Dot(right)*focal/depth;y=self.height()*.5-rel.Dot(up)*focal/depth
            if -18<=x<=self.width()+18 and -18<=y<=self.height()+18:p.drawPixmap(int(x-13),int(y-13),26,26,icon)
        p.end()


class ViewportPanel(QFrame):
    def __init__(self, runtime, scene: bool, localization, resources=None, parent=None) -> None:
        super().__init__(parent); self.setObjectName("ViewportPanel")
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(0)
        if scene:
            controls=QFrame();controls.setObjectName("SceneViewControls");row=QHBoxLayout(controls);row.setContentsMargins(7,3,7,3);row.setSpacing(6)
            grid=QCheckBox("Grid");grid.setChecked(True);plane=QComboBox();plane.addItems(("XY","XZ","YZ"));plane.setCurrentIndex(1);speed=QComboBox();speed.addItems(("1×","2×","5×","10×"));speed.setCurrentIndex(2)
            row.addWidget(grid);row.addWidget(QLabel("Plane"));row.addWidget(plane);row.addStretch();row.addWidget(QLabel("Fly"));row.addWidget(speed);layout.addWidget(controls)
        self.Surface = NativeRenderSurface(runtime, scene); layout.addWidget(self.Surface, 1)
        if scene:
            self.SceneIcons=SceneIconOverlay(self.Surface,runtime,resources);self.Orientation=SceneOrientationWidget(self.Surface)
            self.Surface.CameraChanged.connect(self.Orientation.SetCamera)
            grid.toggled.connect(lambda checked:runtime.SetGrid(checked,plane.currentIndex()))
            plane.currentIndexChanged.connect(lambda index:runtime.SetGrid(grid.isChecked(),index))
            speed.currentIndexChanged.connect(lambda index:self.Surface.SetMoveSpeed((1,2,5,10)[index]))
            self.Surface.SetMoveSpeed(5);runtime.SetGrid(True,1)

    def resizeEvent(self,event)->None:
        super().resizeEvent(event)

    def Detach(self) -> None:
        if hasattr(self,"SceneIcons"):self.SceneIcons.close()
        if hasattr(self,"Orientation"):self.Orientation.close()
        self.Surface.Detach()


__all__ = ["NativeRenderSurface", "ViewportPanel"]
