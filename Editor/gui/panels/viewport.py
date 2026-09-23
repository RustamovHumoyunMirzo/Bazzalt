"""Qt native surfaces hosted by the private Filament editor bridge."""

from __future__ import annotations

from math import cos, radians, sin

from PySide6.QtCore import QPoint, Qt, QTimer
from PySide6.QtGui import QMouseEvent, QWheelEvent
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QToolButton, QVBoxLayout, QWidget


class NativeRenderSurface(QWidget):
    _next_id = 1

    def __init__(self, runtime, scene: bool, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.Runtime = runtime; self.IsScene = scene
        self.ViewportId = NativeRenderSurface._next_id; NativeRenderSurface._next_id += 1
        self._attached = False; self._last = QPoint(); self._yaw = 36.0; self._pitch = -20.0
        self._distance = 12.0; self._target = [0.0, 0.0, 0.0]
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
        self.Runtime.SetSceneCamera(self.ViewportId, eye, tuple(self._target))

    def mousePressEvent(self, event: QMouseEvent) -> None:
        self._last = event.position().toPoint(); self.setFocus()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if not self.IsScene: return
        current = event.position().toPoint(); delta = current - self._last; self._last = current
        if event.buttons() & Qt.MouseButton.RightButton:
            self._yaw += delta.x() * .35; self._pitch = max(-89.0, min(89.0, self._pitch + delta.y() * .35)); self._UpdateCamera()
        elif event.buttons() & Qt.MouseButton.MiddleButton:
            scale = self._distance * .0015
            self._target[0] -= delta.x() * scale; self._target[1] += delta.y() * scale; self._UpdateCamera()

    def wheelEvent(self, event: QWheelEvent) -> None:
        if self.IsScene:
            self._distance = max(.1, min(5000.0, self._distance * (0.88 ** (event.angleDelta().y() / 120.0))))
            self._UpdateCamera(); event.accept()


class ViewportPanel(QFrame):
    def __init__(self, runtime, scene: bool, localization, parent=None) -> None:
        super().__init__(parent); self.setObjectName("ViewportPanel")
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(0)
        if scene:
            controls = QFrame(); controls.setObjectName("SceneControls")
            row = QHBoxLayout(controls); row.setContentsMargins(6, 3, 6, 3); row.setSpacing(3)
            for text, tip in (("Q", "Select"), ("W", "Translate"), ("E", "Rotate"), ("R", "Scale")):
                button = QToolButton(); button.setText(text); button.setToolTip(tip); button.setCheckable(True)
                button.setAutoExclusive(True); row.addWidget(button)
            row.addStretch(); row.addWidget(QLabel("Perspective")); layout.addWidget(controls)
        self.Surface = NativeRenderSurface(runtime, scene); layout.addWidget(self.Surface, 1)

    def Detach(self) -> None: self.Surface.Detach()


__all__ = ["NativeRenderSurface", "ViewportPanel"]
