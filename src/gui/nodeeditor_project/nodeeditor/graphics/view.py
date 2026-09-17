from PySide6.QtCore import QEvent, QPointF, Qt, Signal
from PySide6.QtGui import QAction, QPainter, QTransform
from PySide6.QtWidgets import QGraphicsView, QMenu

from .socket_item import GraphicsSocket
from .node_item import GraphicsNode
from .edge_item import GraphicsEdge

ZOOM_MIN, ZOOM_MAX = 0.15, 3.0


class NodeView(QGraphicsView):
    node_context_requested = Signal(object, object)     # GraphicsNode, scene_pos
    canvas_context_requested = Signal(object)            # scene_pos

    def __init__(self, scene, parent=None):
        super().__init__(scene, parent)
        self.node_scene = scene
        self.setRenderHints(QPainter.RenderHint.Antialiasing | QPainter.RenderHint.TextAntialiasing)
        self.setDragMode(QGraphicsView.DragMode.RubberBandDrag)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorViewCenter)
        self.setViewportUpdateMode(QGraphicsView.ViewportUpdateMode.FullViewportUpdate)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setFrameShape(self.Shape.NoFrame)

        self._zoom = 1.0
        self._panning = False
        self._pan_start = QPointF()
        self._space_held = False
        self._connecting_from_socket = None

        self.customContextMenuRequested.connect(self._on_context_menu)

    # ---------------------------------------------------------------- zoom

    def wheelEvent(self, event):
        factor = 1.15 if event.angleDelta().y() > 0 else 1 / 1.15
        new_zoom = self._zoom * factor
        if ZOOM_MIN <= new_zoom <= ZOOM_MAX:
            self._zoom = new_zoom
            self.scale(factor, factor)

    def reset_zoom(self):
        self.resetTransform()
        self._zoom = 1.0

    # ----------------------------------------------------------------- pan

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Space and not event.isAutoRepeat():
            self._space_held = True
            self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)
        elif event.key() in (Qt.Key.Key_Delete, Qt.Key.Key_Backspace):
            self._delete_selection()
        elif event.key() == Qt.Key.Key_F:
            self._frame_all()
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key.Key_Space and not event.isAutoRepeat():
            self._space_held = False
            self.setDragMode(QGraphicsView.DragMode.RubberBandDrag)
        else:
            super().keyReleaseEvent(event)

    def _frame_all(self):
        items_rect = self.node_scene.itemsBoundingRect()
        if not items_rect.isEmpty():
            self.fitInView(items_rect.adjusted(-60, -60, 60, 60), Qt.AspectRatioMode.KeepAspectRatio)
            self._zoom = self.transform().m11()

    def _delete_selection(self):
        for item in list(self.node_scene.selectedItems()):
            if isinstance(item, GraphicsNode):
                self.node_scene.graph.remove_node(item.node.id)
            elif isinstance(item, GraphicsEdge) and item.connection is not None:
                self.node_scene.graph.remove_connection(item.connection.id)

    # ------------------------------------------------------- mouse handling

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._pan_start = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()
            return

        if event.button() == Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            if isinstance(item, GraphicsSocket):
                self._connecting_from_socket = item
                self.node_scene.begin_drag_connection(item)
                event.accept()
                return

        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._panning:
            delta = event.position() - self._pan_start
            self._pan_start = event.position()
            hbar, vbar = self.horizontalScrollBar(), self.verticalScrollBar()
            hbar.setValue(hbar.value() - int(delta.x()))
            vbar.setValue(vbar.value() - int(delta.y()))
            event.accept()
            return

        if self._connecting_from_socket is not None:
            scene_pos = self.mapToScene(event.position().toPoint())
            self.node_scene.update_drag_connection(scene_pos)
            event.accept()
            return

        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton and self._panning:
            self._panning = False
            self.setCursor(Qt.CursorShape.ArrowCursor)
            event.accept()
            return

        if self._connecting_from_socket is not None:
            item = self.itemAt(event.position().toPoint())
            target = item if isinstance(item, GraphicsSocket) else None
            self.node_scene.end_drag_connection(target)
            self._connecting_from_socket = None
            event.accept()
            return

        super().mouseReleaseEvent(event)

    # -------------------------------------------------------------- context

    def _on_context_menu(self, pos):
        scene_pos = self.mapToScene(pos)
        item = self.itemAt(pos)
        node_item = item
        while node_item is not None and not isinstance(node_item, GraphicsNode):
            node_item = node_item.parentItem()

        if node_item is not None:
            self.node_context_requested.emit(node_item, scene_pos)
        elif isinstance(item, GraphicsEdge):
            menu = QMenu(self)
            act = menu.addAction("Delete Connection")
            chosen = menu.exec(self.mapToGlobal(pos))
            if chosen is act and item.connection is not None:
                self.node_scene.graph.remove_connection(item.connection.id)
        else:
            self.canvas_context_requested.emit(scene_pos)
