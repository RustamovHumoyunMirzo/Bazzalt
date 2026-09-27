"""Qt native surfaces hosted by the private Filament editor bridge."""

from __future__ import annotations

from math import cos, radians, sin, tan

from PySide6.QtCore import QPoint, QPointF, QRect, Qt, QTimer, Signal
from PySide6.QtGui import QKeyEvent, QMouseEvent, QWheelEvent
from PySide6.QtWidgets import (QCheckBox, QComboBox, QFrame, QHBoxLayout, QLabel,
                               QRubberBand, QStackedLayout, QVBoxLayout, QWidget)
from ..gizmos import GizmoDrag, GizmoHandle, GizmoMode, PickAxis, PickRotationAxis, Ray, Vec3


class NativeRenderSurface(QWidget):
    TranslationDragged = Signal(object)
    RotationDragged = Signal(object, float)
    ScaleDragged = Signal(object)
    GizmoDragFinished = Signal()
    CameraChanged = Signal(float, float)
    Attached = Signal()
    EntityPicked = Signal(str)
    EntitiesBoxSelected = Signal(object, bool)
    SelectionBoxStarted = Signal(bool)
    GizmoDragStarted = Signal()
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
        self._navigating=False;self._keys=set();self._move_speed=5.0
        self._fly_timer=QTimer(self);self._fly_timer.setInterval(16);self._fly_timer.timeout.connect(self._FlyTick);self._fly_timer.start()
        self.setAttribute(Qt.WidgetAttribute.WA_DontCreateNativeAncestors)
        self.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)

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

    def _PickOrientation(self,point:QPoint)->bool:
        if point.x()<self.width()-104 or point.y()>104:return False
        cy,sy=cos(radians(self._yaw)),sin(radians(self._yaw));cp,sp=cos(radians(self._pitch)),sin(radians(self._pitch));center=QPointF(self.width()-48,48)
        def project(v):
            x,y,z=v;x,z=x*cy-z*sy,x*sy+z*cy;y,z=y*cp-z*sp,y*sp+z*cp
            return QPointF(center.x()+x*28,center.y()-y*28)
        axes=((project((1,0,0)),(90.,0.)),(project((0,1,0)),(self._yaw,89.)),(project((0,0,1)),(0.,0.)))
        position=QPointF(point);_end,target=min(axes,key=lambda item:(item[0]-position).manhattanLength())
        if (_end-position).manhattanLength()>22:return False
        start_yaw=self._yaw;delta=(target[0]-start_yaw+180)%360-180;start_pitch=self._pitch;step=0
        timer=QTimer(self);timer.setInterval(16)
        def tick():
            nonlocal step;step+=1;t=min(1.,step/12.);ease=1-(1-t)**3
            self._yaw=start_yaw+delta*ease;self._pitch=start_pitch+(target[1]-start_pitch)*ease;self._UpdateCamera()
            if t>=1:timer.stop()
        timer.timeout.connect(tick);self._orientation_animation=timer;timer.start();return True

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._last = event.position().toPoint(); self.setFocus()
        if self.IsScene and event.button()==Qt.MouseButton.RightButton:self._navigating=True;event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and self._PickOrientation(self._last):event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and self._selection and self._mode is not GizmoMode.Select:
            ray=self._Ray(self._last);handle=self._PickGizmo(self._last)
            if handle is not None:
                self._SetHover(handle);self.setCursor(Qt.CursorShape.ClosedHandCursor)
                self.GizmoDragStarted.emit()
                self._gizmo_drag=GizmoDrag(self._mode,handle,ray,self._selection,(Vec3(*self._target)-Vec3(*self._eye)).Normalized());self._last_delta=Vec3();self._last_angle=0.0;self._last_scale=Vec3(1,1,1);event.accept();return
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton:
            entity_id=self._PickSceneIcon(self._last)
            if entity_id:self.EntityPicked.emit(entity_id)
            else:
                self._selection_box_start=self._last;self._selection_box_additive=bool(event.modifiers()&Qt.KeyboardModifier.ControlModifier)
                self.SelectionBoxStarted.emit(self._selection_box_additive)
            event.accept();return

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if not self.IsScene: return
        current = event.position().toPoint(); delta = current - self._last; self._last = current
        if self._selection_box_start is not None and event.buttons()&Qt.MouseButton.LeftButton:
            if self._selection_band is None:
                self._selection_band=QRubberBand(QRubberBand.Shape.Rectangle,self)
                self._selection_band.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
                self._selection_band.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
                self._selection_band.setStyleSheet("QRubberBand { background-color: rgba(55, 135, 235, 38); border: 1px solid rgba(105, 180, 255, 220); }")
            self._selection_band.setGeometry(QRect(self._selection_box_start,current).normalized());self._selection_band.show();return
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
            if self._selection_box_start is not None:
                start=self._selection_box_start;self._selection_box_start=None;end=event.position().toPoint()
                if self._selection_band is not None:self._selection_band.hide()
                left,right=sorted((start.x(),end.x()));top,bottom=sorted((start.y(),end.y()));selected=[]
                if right-left>4 or bottom-top>4:
                    for entity in self.Runtime.Entities():
                        projected=self._Project(entity.get("world_position",entity.get("position",(0,0,0))))
                        if projected and left<=projected[0]<=right and top<=projected[1]<=bottom:selected.append(str(entity.get("uuid","")))
                self.EntitiesBoxSelected.emit(selected,self._selection_box_additive);event.accept();return
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


class ViewportPanel(QFrame):
    def __init__(self, runtime, scene: bool, localization, resources=None, parent=None) -> None:
        super().__init__(parent); self.setObjectName("ViewportPanel")
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(0)
        if scene:
            controls=QFrame();controls.setObjectName("SceneViewControls");row=QHBoxLayout(controls);row.setContentsMargins(7,3,7,3);row.setSpacing(6)
            grid=QCheckBox("Grid");grid.setChecked(True);plane=QComboBox();plane.addItems(("XY","XZ","YZ"));plane.setCurrentIndex(1);speed=QComboBox();speed.addItems(("1×","2×","5×","10×"));speed.setCurrentIndex(2)
            row.addWidget(grid);row.addWidget(QLabel("Plane"));row.addWidget(plane);row.addStretch();row.addWidget(QLabel("Fly"));row.addWidget(speed);layout.addWidget(controls)
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
            speed.currentIndexChanged.connect(lambda index:self.Surface.SetMoveSpeed((1,2,5,10)[index]))
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
