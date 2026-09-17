from PySide6.QtCore import QRectF, Qt, QPointF
from PySide6.QtGui import QBrush, QColor, QPainterPath, QPen
from PySide6.QtWidgets import QGraphicsItem

RADIUS = 6.0


class GraphicsSocket(QGraphicsItem):
    """Visual representation of one Port, drawn on a node's edge."""

    def __init__(self, port, node_item, theme):
        super().__init__(node_item)
        self.port = port
        self.node_item = node_item
        self.theme = theme
        self.setAcceptHoverEvents(True)
        self._hovered = False
        self.setToolTip(f"{port.name}  ({port.socket_type.name})")

    def boundingRect(self) -> QRectF:
        return QRectF(-RADIUS - 2, -RADIUS - 2, (RADIUS + 2) * 2, (RADIUS + 2) * 2)

    def scene_center(self) -> QPointF:
        return self.mapToScene(QPointF(0, 0))

    def _shape_path(self) -> QPainterPath:
        path = QPainterPath()
        shape = self.port.socket_type.shape
        r = RADIUS if not self._hovered else RADIUS * 1.15
        if shape == "square":
            path.addRect(-r, -r, r * 2, r * 2)
        elif shape == "diamond":
            path.moveTo(0, -r)
            path.lineTo(r, 0)
            path.lineTo(0, r)
            path.lineTo(-r, 0)
            path.closeSubpath()
        elif shape == "triangle":
            path.moveTo(-r, -r)
            path.lineTo(r, 0)
            path.lineTo(-r, r)
            path.closeSubpath()
        else:
            path.addEllipse(-r, -r, r * 2, r * 2)
        return path

    def paint(self, painter, option, widget=None):
        color = QColor(self.port.socket_type.color)
        painter.setRenderHint(painter.RenderHint.Antialiasing)
        painter.setBrush(QBrush(color))
        pen = QPen(QColor(self.theme.socket_border))
        pen.setWidthF(1.2)
        painter.setPen(pen)
        painter.drawPath(self._shape_path())

    def hoverEnterEvent(self, event):
        self._hovered = True
        self.update()
        super().hoverEnterEvent(event)

    def hoverLeaveEvent(self, event):
        self._hovered = False
        self.update()
        super().hoverLeaveEvent(event)
