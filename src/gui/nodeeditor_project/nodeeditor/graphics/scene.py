from PySide6.QtCore import QLineF, QRectF, Qt, Signal, QObject
from PySide6.QtGui import QColor, QPen
from PySide6.QtWidgets import QGraphicsScene

from ..graph import NodeGraph, GraphError
from .node_item import GraphicsNode
from .edge_item import GraphicsEdge
from .socket_item import GraphicsSocket

SCENE_EXTENT = 32000


class SceneSignals(QObject):
    node_selected = Signal(object)
    graph_changed = Signal()


class NodeScene(QGraphicsScene):
    def __init__(self, graph: NodeGraph, theme, parent=None):
        super().__init__(parent)
        self.graph = graph
        self.theme = theme
        self.signals = SceneSignals()

        self.node_items = {}        # Node -> GraphicsNode
        self.connection_items = {}  # Connection -> GraphicsEdge

        self._drag_edge = None
        self._drag_from_port = None

        self.setSceneRect(-SCENE_EXTENT, -SCENE_EXTENT, SCENE_EXTENT * 2, SCENE_EXTENT * 2)
        self.apply_theme(theme)

        self.graph.on_change(self._on_graph_event)
        self._sync_all()

    # ----------------------------------------------------------------- theme

    def apply_theme(self, theme):
        self.theme = theme
        self.setBackgroundBrush(QColor(theme.background))
        for gnode in self.node_items.values():
            gnode.apply_theme(theme)
        for gedge in self.connection_items.values():
            gedge.theme = theme
        self.update()

    def drawBackground(self, painter, rect: QRectF):
        super().drawBackground(painter, rect)
        painter.fillRect(rect, QColor(self.theme.background))

        def draw_grid(spacing, color):
            pen = QPen(QColor(color))
            pen.setWidthF(1.0)
            painter.setPen(pen)
            left = int(rect.left()) - (int(rect.left()) % spacing)
            top = int(rect.top()) - (int(rect.top()) % spacing)
            lines = []
            x = left
            while x < rect.right():
                lines.append(QLineF(x, rect.top(), x, rect.bottom()))
                x += spacing
            y = top
            while y < rect.bottom():
                lines.append(QLineF(rect.left(), y, rect.right(), y))
                y += spacing
            if lines:
                painter.drawLines(lines)

        draw_grid(self.theme.grid_fine_spacing, self.theme.grid_fine)
        draw_grid(self.theme.grid_coarse_spacing, self.theme.grid_coarse)

    # ------------------------------------------------------------ graph sync

    def _on_graph_event(self, event, payload):
        if event == "node_added":
            self._add_node_item(payload)
        elif event == "node_removed":
            self._remove_node_item(payload)
        elif event == "connection_added":
            self._add_connection_item(payload)
        elif event == "connection_removed":
            self._remove_connection_item(payload)
        elif event == "cleared":
            self._sync_all()
        self.signals.graph_changed.emit()

    def _sync_all(self):
        for item in list(self.node_items.values()):
            self.removeItem(item)
        for item in list(self.connection_items.values()):
            self.removeItem(item)
        self.node_items.clear()
        self.connection_items.clear()
        for node in self.graph.nodes.values():
            self._add_node_item(node)
        for conn in self.graph.connections:
            self._add_connection_item(conn)

    def _add_node_item(self, node):
        gnode = GraphicsNode(node, self.theme, self)
        gnode.signals.moved.connect(lambda gn=gnode: self.update_edges_for_node(gn))
        self.addItem(gnode)
        self.node_items[node] = gnode
        node.graphics = gnode

    def _remove_node_item(self, node):
        gnode = self.node_items.pop(node, None)
        if gnode is not None:
            self.removeItem(gnode)

    def _add_connection_item(self, conn):
        gedge = GraphicsEdge(self.theme, connection=conn)
        out_gnode = self.node_items.get(conn.output_port.node)
        in_gnode = self.node_items.get(conn.input_port.node)
        if out_gnode and in_gnode:
            gedge.source_socket = out_gnode.socket_items.get(conn.output_port)
            gedge.target_socket = in_gnode.socket_items.get(conn.input_port)
        gedge.update_path()
        self.addItem(gedge)
        self.connection_items[conn] = gedge
        conn.graphics = gedge

    def _remove_connection_item(self, conn):
        gedge = self.connection_items.pop(conn, None)
        if gedge is not None:
            self.removeItem(gedge)

    def update_edges_for_node(self, gnode: GraphicsNode):
        for conn, gedge in self.connection_items.items():
            if conn.output_port.node is gnode.node or conn.input_port.node is gnode.node:
                out_gnode = self.node_items.get(conn.output_port.node)
                in_gnode = self.node_items.get(conn.input_port.node)
                gedge.source_socket = out_gnode.socket_items.get(conn.output_port) if out_gnode else None
                gedge.target_socket = in_gnode.socket_items.get(conn.input_port) if in_gnode else None
                gedge.update_path()

    # ----------------------------------------------------- interactive wiring

    def socket_at(self, scene_pos) -> "GraphicsSocket | None":
        for item in self.items(scene_pos):
            if isinstance(item, GraphicsSocket):
                return item
        return None

    def begin_drag_connection(self, socket_item: GraphicsSocket):
        self._drag_from_port = socket_item.port
        self._drag_edge = GraphicsEdge(self.theme)
        if socket_item.port.is_input:
            self._drag_edge.target_socket = socket_item
        else:
            self._drag_edge.source_socket = socket_item
        self._drag_edge.set_drag_end(socket_item.scene_center())
        self.addItem(self._drag_edge)

    def update_drag_connection(self, scene_pos):
        if self._drag_edge is not None:
            self._drag_edge.set_drag_end(scene_pos)

    def end_drag_connection(self, target_socket: "GraphicsSocket | None"):
        if self._drag_edge is not None:
            self.removeItem(self._drag_edge)
            self._drag_edge = None

        from_port = self._drag_from_port
        self._drag_from_port = None
        if from_port is None or target_socket is None:
            return None

        a, b = from_port, target_socket.port
        if a.is_input == b.is_input:
            return None  # must connect an output to an input
        output_port, input_port = (a, b) if not a.is_input else (b, a)
        try:
            return self.graph.connect(output_port, input_port)
        except GraphError:
            return None

    def cancel_drag_connection(self):
        if self._drag_edge is not None:
            self.removeItem(self._drag_edge)
            self._drag_edge = None
        self._drag_from_port = None
