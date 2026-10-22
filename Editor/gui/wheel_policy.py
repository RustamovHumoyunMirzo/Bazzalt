"""Unfocused value controls must scroll their containing panel, not edit data."""
from PySide6.QtCore import QObject,QEvent,Qt,QPointF
from PySide6.QtGui import QWheelEvent
from PySide6.QtWidgets import QApplication,QWidget,QAbstractSpinBox,QComboBox,QSlider,QAbstractScrollArea

class EditorWheelPolicy(QObject):
    def __init__(self,editor):super().__init__(editor);self.Editor=editor;self.Forwarding=False
    def eventFilter(self,watched,event):
        if self.Forwarding or not isinstance(watched,QWidget):return False
        if watched is not self.Editor and not self.Editor.isAncestorOf(watched):return False
        control=watched
        if not isinstance(control,(QAbstractSpinBox,QComboBox,QSlider)):
            control=watched.parentWidget()
            if not isinstance(control,(QAbstractSpinBox,QComboBox,QSlider)):return False
        if event.type() in (QEvent.Type.Polish,QEvent.Type.Show):
            control.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        if event.type()!=QEvent.Type.Wheel:return False
        focus=QApplication.focusWidget()
        if focus is control or (focus is not None and control.isAncestorOf(focus)):return False
        parent=control.parentWidget()
        while parent is not None and not isinstance(parent,QAbstractScrollArea):parent=parent.parentWidget()
        if parent is not None:
            viewport=parent.viewport();position=QPointF(viewport.mapFromGlobal(event.globalPosition().toPoint()))
            forwarded=QWheelEvent(position,event.globalPosition(),event.pixelDelta(),event.angleDelta(),event.buttons(),event.modifiers(),event.phase(),event.inverted(),event.source())
            self.Forwarding=True
            try:QApplication.sendEvent(viewport,forwarded)
            finally:self.Forwarding=False
        event.accept();return True
