"""Qt native surfaces hosted by the private Filament editor bridge."""

from __future__ import annotations

from math import cos, radians, sin, tan

from PySide6.QtCore import QPoint, Qt, QTimer, Signal
from PySide6.QtGui import QMouseEvent, QWheelEvent
from PySide6.QtWidgets import QFrame, QVBoxLayout, QWidget
from ..gizmos import GizmoDrag, GizmoMode, PickAxis, Ray, Vec3


class NativeRenderSurface(QWidget):
    TranslationDragged = Signal(object)
    RotationDragged = Signal(object, float)
    ScaleDragged = Signal(object)
    _next_id = 1

    def __init__(self, runtime, scene: bool, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.Runtime = runtime; self.IsScene = scene
        self.ViewportId = NativeRenderSurface._next_id; NativeRenderSurface._next_id += 1
        self._attached = False; self._last = QPoint(); self._yaw = 36.0; self._pitch = -20.0
        self._distance = 12.0; self._target = [0.0, 0.0, 0.0]
        self._eye = (6.0,4.0,8.0); self._selection = None; self._mode = GizmoMode.Select
        self._gizmo_drag = None; self._last_delta = Vec3(); self._last_angle=0.0;self._last_scale=Vec3(1,1,1)
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
        self._eye=eye;self.Runtime.SetSceneCamera(self.ViewportId, eye, tuple(self._target))

    def SetSelection(self, position) -> None: self._selection = Vec3(*position) if position is not None else None
    def SetGizmoMode(self, mode: GizmoMode) -> None: self._mode = mode

    def _Ray(self, point: QPoint) -> Ray:
        eye=Vec3(*self._eye);target=Vec3(*self._target);forward=(target-eye).Normalized()
        right=forward.Cross(Vec3(0,1,0)).Normalized();up=right.Cross(forward).Normalized()
        x=2.0*point.x()/max(1,self.width())-1.0;y=1.0-2.0*point.y()/max(1,self.height())
        spread=tan(radians(30.0));aspect=self.width()/max(1,self.height())
        return Ray(eye,(forward+right*(x*spread*aspect)+up*(y*spread)).Normalized())

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._last = event.position().toPoint(); self.setFocus()
        if self.IsScene and event.button()==Qt.MouseButton.LeftButton and self._selection and self._mode is not GizmoMode.Select:
            ray=self._Ray(self._last);handle=PickAxis(ray,self._selection,1.5,max(.08,self._distance*.012))
            if handle is not None:
                self._gizmo_drag=GizmoDrag(self._mode,handle,ray,self._selection,(Vec3(*self._target)-Vec3(*self._eye)).Normalized());self._last_delta=Vec3();self._last_angle=0.0;self._last_scale=Vec3(1,1,1);event.accept()

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
            self._yaw -= delta.x() * .35; self._pitch = max(-89.0, min(89.0, self._pitch - delta.y() * .35)); self._UpdateCamera()
        elif event.buttons() & Qt.MouseButton.MiddleButton:
            scale = self._distance * .0015
            self._target[0] += delta.x() * scale; self._target[1] -= delta.y() * scale; self._UpdateCamera()

    def wheelEvent(self, event: QWheelEvent) -> None:
        if self.IsScene:
            self._distance = max(.1, min(5000.0, self._distance * (0.88 ** (event.angleDelta().y() / 120.0))))
            self._UpdateCamera(); event.accept()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button()==Qt.MouseButton.LeftButton:self._gizmo_drag=None


class ViewportPanel(QFrame):
    def __init__(self, runtime, scene: bool, localization, parent=None) -> None:
        super().__init__(parent); self.setObjectName("ViewportPanel")
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(0)
        self.Surface = NativeRenderSurface(runtime, scene); layout.addWidget(self.Surface, 1)

    def Detach(self) -> None: self.Surface.Detach()


__all__ = ["NativeRenderSurface", "ViewportPanel"]
