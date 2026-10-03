"""Reusable, surface-safe inspector image viewport with pan/zoom navigation."""
from PySide6.QtCore import Qt,QPointF,QRectF
from PySide6.QtGui import QImage,QPainter,QColor
from PySide6.QtWidgets import QFrame,QSizePolicy


class InspectorViewport(QFrame):
    def __init__(self,parent=None,empty_text=""):
        super().__init__(parent);self.setObjectName("InspectorViewport")
        self.setMinimumHeight(150);self.setMaximumHeight(280);self.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Preferred)
        self._image=QImage();self._clear=None;self._empty=empty_text
        self._zoom=1.;self._pan=QPointF();self._drag=None
    def SetImage(self,value):
        self._image=QImage(value) if value is not None else QImage();self.ResetView()
    def SetClearColor(self,value):
        self._clear=QColor(value) if isinstance(value,QColor) else QColor.fromRgbF(*value);self.update()
    def ResetView(self):self._zoom=1.;self._pan=QPointF();self.update()
    def HasImage(self):return not self._image.isNull()
    def BackgroundColor(self):return self.palette().window().color().darker(125)
    def paintEvent(self,event):
        painter=QPainter(self);painter.fillRect(self.rect(),self.BackgroundColor())
        content=QRectF(self.rect()).adjusted(8,8,-8,-8)
        painter.setClipRect(content)
        if self._clear is not None:painter.fillRect(content,self._clear)
        if self.HasImage():
            scale=min(content.width()/self._image.width(),content.height()/self._image.height())*self._zoom
            width,height=self._image.width()*scale,self._image.height()*scale
            target=QRectF(content.center().x()-width/2+self._pan.x(),content.center().y()-height/2+self._pan.y(),width,height)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform);painter.drawImage(target,self._image)
        elif self._empty:
            painter.setPen(self.palette().text().color());painter.drawText(self.rect().adjusted(8,8,-8,-8),Qt.AlignmentFlag.AlignCenter|Qt.TextFlag.TextWordWrap,self._empty)
        painter.end()
    def wheelEvent(self,event):self._zoom=max(.25,min(8.,self._zoom*(1.15 if event.angleDelta().y()>0 else 1/1.15)));self.update();event.accept()
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self._drag=event.position();event.accept()
        else:super().mousePressEvent(event)
    def mouseMoveEvent(self,event):
        if self._drag is not None:self._pan+=event.position()-self._drag;self._drag=event.position();self.update();event.accept()
        else:super().mouseMoveEvent(event)
    def mouseReleaseEvent(self,event):self._drag=None;super().mouseReleaseEvent(event)
    def mouseDoubleClickEvent(self,event):self.ResetView();event.accept()
