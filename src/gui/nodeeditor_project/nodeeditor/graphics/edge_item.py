from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QPainterPath, QPen
from PySide6.QtWidgets import QGraphicsPathItem


class GraphicsEdge(QGraphicsPathItem):
    """A bezier wire between two GraphicsSocket items (or a floating end
    point while being dragged)."""

    def __init__(self, theme, connection=None):
        super().__init__()
        self.theme = theme
        self.connection = connection  # None while being dragged interactively
        self.source_socket = None
        self.target_socket = None
        self._drag_end_pos = None
        self.setZValue(-1)
        self.setFlag(QGraphicsPathItem.GraphicsItemFlag.ItemIsSelectable, True)
        self.setAcceptHoverEvents(True)
        self._hovered = False

    def set_drag_end(self, scene_pos: QPointF):
        self._drag_end_pos = scene_pos
        self.update_path()

    def endpoints(self):
        start = self.source_socket.scene_center() if self.source_socket else QPointF()
        if self.target_socket is not None:
            end = self.target_socket.scene_center()
        elif self._drag_end_pos is not None:
            end = self._drag_end_pos
        else:
            end = start
        return start, end

    def update_path(self):
        start, end = self.endpoints()
        path = QPainterPath(start)
        dx = max(abs(end.x() - start.x()) * 0.5, 40.0)
        c1 = QPointF(start.x() + dx, start.y())
        c2 = QPointF(end.x() - dx, end.y())
        path.cubicTo(c1, c2, end)
        self.setPath(path)

    def hoverEnterEvent(self, event):
        self._hovered = True
        self.update()
        super().hoverEnterEvent(event)

    def hoverLeaveEvent(self, event):
        self._hovered = False
        self.update()
        super().hoverLeaveEvent(event)

    def paint(self, painter, option, widget=None):
        painter.setRenderHint(painter.RenderHint.Antialiasing)
        if self.isSelected():
            color = QColor(self.theme.connection_selected)
            width = self.theme.connection_width + 0.8
        elif self.connection is None:
            color = QColor(self.theme.connection_dragging)
            width = self.theme.connection_width
        elif self._hovered:
            color = QColor(self.theme.connection_selected)
            width = self.theme.connection_width + 0.4
        else:
            color = QColor(self.theme.connection_default)
            width = self.theme.connection_width

        pen = QPen(color)
        pen.setWidthF(width)
        pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        painter.setPen(pen)
        painter.drawPath(self.path())
