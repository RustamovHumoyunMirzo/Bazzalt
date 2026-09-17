from PySide6.QtCore import QRectF, Qt, QPointF, Signal, QObject
from PySide6.QtGui import QBrush, QColor, QFont, QPainterPath, QPen
from PySide6.QtWidgets import (
    QGraphicsItem, QGraphicsProxyWidget, QGraphicsTextItem, QLineEdit,
    QDoubleSpinBox, QSpinBox, QCheckBox, QComboBox, QGraphicsSimpleTextItem,
)

from .socket_item import GraphicsSocket

HEADER_H = 28.0
ROW_H = 22.0
PAD = 10.0
MIN_WIDTH = 160.0


class NodeSignals(QObject):
    moved = Signal(object)
    selected_changed = Signal(object, bool)


class GraphicsNode(QGraphicsItem):
    def __init__(self, node, theme, scene_ref):
        super().__init__()
        self.node = node
        self.theme = theme
        self.scene_ref = scene_ref  # NodeScene, for edge-update callbacks
        self.signals = NodeSignals()

        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges, True)
        self.setZValue(0)

        self._width = MIN_WIDTH
        self.socket_items = {}   # Port -> GraphicsSocket
        self.value_widgets = {}  # Port -> QGraphicsProxyWidget

        self._title_item = QGraphicsTextItem(self)
        self._collapse_item = QGraphicsSimpleTextItem(self)
        self._collapse_item.setCursor(Qt.CursorShape.PointingHandCursor)

        self.setPos(node.x, node.y)
        self.rebuild()

    # ---------------------------------------------------------------- layout

    def rebuild(self):
        """(Re)build child items from the current node/port state."""
        scene = self.scene()
        for child in list(self.childItems()):
            if child in (self._title_item, self._collapse_item):
                continue
            if scene is not None:
                scene.removeItem(child)
            else:
                child.setParentItem(None)
        self.socket_items.clear()
        self.value_widgets.clear()

        self._title_item.setPlainText(self.node.title)
        self._title_item.setDefaultTextColor(QColor(self.theme.node_header_text))
        font = QFont("Segoe UI", 9)
        font.setBold(True)
        self._title_item.setFont(font)
        self._title_item.setPos(PAD, 5)

        self._collapse_item.setBrush(QBrush(QColor(self.theme.node_header_text)))
        self._collapse_item.setText("\u25be" if not self.node.collapsed else "\u25b8")

        if not self.node.collapsed:
            row_count = max(len(self.node.inputs), len(self.node.outputs))
            body_h = row_count * ROW_H + PAD
            self._height = HEADER_H + body_h
            self._layout_ports()
        else:
            self._height = HEADER_H

        self._width = self._compute_width()
        self._collapse_item.setPos(self._width - 18, 8)
        self.prepareGeometryChange()
        self.update()

    def apply_theme(self, theme):
        """Refresh colors in place (no child-item reconstruction)."""
        self.theme = theme
        self._title_item.setDefaultTextColor(QColor(theme.node_header_text))
        self._collapse_item.setBrush(QBrush(QColor(theme.node_header_text)))
        for child in self.childItems():
            if isinstance(child, QGraphicsSimpleTextItem) and child is not self._collapse_item:
                child.setBrush(QBrush(QColor(theme.socket_label_text)))
        for sock in self.socket_items.values():
            sock.theme = theme
        self.update()

    def _compute_width(self) -> float:
        widest = 40 + self._title_item.boundingRect().width()
        for port in list(self.node.inputs) + list(self.node.outputs):
            widest = max(widest, 70 + len(port.name) * 7)
        return max(MIN_WIDTH, widest)

    def _layout_ports(self):
        for i, port in enumerate(self.node.inputs):
            y = HEADER_H + PAD / 2 + i * ROW_H + ROW_H / 2
            sock = GraphicsSocket(port, self, self.theme)
            sock.setPos(0, y)
            self.socket_items[port] = sock

            label = QGraphicsSimpleTextItem(port.spec.display_label(), self)
            label.setBrush(QBrush(QColor(self.theme.socket_label_text)))
            label.setPos(12, y - 8)

            if not self._has_incoming(port):
                widget = self._make_value_widget(port)
                if widget is not None:
                    proxy = QGraphicsProxyWidget(self)
                    proxy.setWidget(widget)
                    label.setVisible(False)
                    proxy.setPos(10, y - 9)
                    self.value_widgets[port] = proxy

        for i, port in enumerate(self.node.outputs):
            y = HEADER_H + PAD / 2 + i * ROW_H + ROW_H / 2
            sock = GraphicsSocket(port, self, self.theme)
            self.socket_items[port] = sock  # x set once width is known in paint-time layout

            label = QGraphicsSimpleTextItem(port.spec.display_label(), self)
            label.setBrush(QBrush(QColor(self.theme.socket_label_text)))
            label.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIgnoresTransformations, False)
            label._is_output_label = True
            label._row_y = y
            sock.setPos(0, y)
            sock._pending_right_align = True
            label._pending_right_align = True

        # second pass: right-align outputs now that width is known
        self._realign_outputs()

    def _realign_outputs(self):
        w = self._width if hasattr(self, "_width") else self._compute_width()
        for port, sock in self.socket_items.items():
            if not port.is_input:
                sock.setPos(w, sock.pos().y())
        for child in self.childItems():
            if isinstance(child, QGraphicsSimpleTextItem) and getattr(child, "_pending_right_align", False):
                text_w = child.boundingRect().width()
                child.setPos(w - 12 - text_w, child._row_y - 8)

    def _has_incoming(self, port) -> bool:
        return bool(self.scene_ref.graph.connections_to(port))

    def _make_value_widget(self, port):
        t = port.spec.type
        default = port.value if port.value is not None else port.spec.default
        w = None
        if t == "float":
            w = QDoubleSpinBox()
            w.setRange(-1e9, 1e9)
            w.setDecimals(3)
            w.setValue(float(default or 0.0))
            w.valueChanged.connect(lambda v, p=port: self._commit_value(p, float(v)))
        elif t == "int":
            w = QSpinBox()
            w.setRange(-1_000_000_000, 1_000_000_000)
            w.setValue(int(default or 0))
            w.valueChanged.connect(lambda v, p=port: self._commit_value(p, int(v)))
        elif t == "bool":
            w = QCheckBox(port.spec.display_label())
            w.setChecked(bool(default))
            w.toggled.connect(lambda v, p=port: self._commit_value(p, bool(v)))
        elif t == "string":
            w = QLineEdit(str(default) if default is not None else "")
            w.setPlaceholderText(port.spec.display_label())
            w.textChanged.connect(lambda v, p=port: self._commit_value(p, v))
        else:
            return None

        w.setFixedWidth(int(self._width - 22)) if hasattr(self, "_width") else None
        w.setStyleSheet(
            "QWidget { background: transparent; color: %s; font-size: 11px; }"
            "QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {"
            " background: rgba(255,255,255,18); border: 1px solid %s; border-radius: 4px; padding: 1px 4px; }"
            % (self.theme.node_body_text, self.theme.node_border)
        )
        return w

    def _commit_value(self, port, value):
        port.value = value

    # -------------------------------------------------------------- painting

    def boundingRect(self) -> QRectF:
        return QRectF(0, 0, self._width, self._height)

    def paint(self, painter, option, widget=None):
        painter.setRenderHint(painter.RenderHint.Antialiasing)
        r = self.theme.node_corner_radius
        rect = self.boundingRect()

        path = QPainterPath()
        path.addRoundedRect(rect, r, r)

        bg = QColor(self.theme.node_bg_selected if self.isSelected() else self.theme.node_bg)
        painter.setBrush(QBrush(bg))
        border_color = QColor(self.theme.node_border_selected if self.isSelected() else self.theme.node_border)
        pen = QPen(border_color)
        pen.setWidthF(1.6 if self.isSelected() else 1.0)
        painter.setPen(pen)
        painter.drawPath(path)

        header_path = QPainterPath()
        header_rect = QRectF(0, 0, self._width, min(HEADER_H, self._height))
        header_path.addRoundedRect(header_rect, r, r)
        # square off the bottom corners of the header block
        header_path.addRect(0, HEADER_H - r, self._width, r)
        header_color = QColor(self.node.node_type.color or self.theme.node_header_default)
        painter.setBrush(QBrush(header_color))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPath(header_path.intersected(path))

    # -------------------------------------------------------------- behavior

    def itemChange(self, change, value):
        if change == QGraphicsItem.GraphicsItemChange.ItemPositionHasChanged:
            self.node.x = self.pos().x()
            self.node.y = self.pos().y()
            self.signals.moved.emit(self)
        elif change == QGraphicsItem.GraphicsItemChange.ItemSelectedHasChanged:
            self.signals.selected_changed.emit(self, bool(value))
        return super().itemChange(change, value)

    def mouseDoubleClickEvent(self, event):
        local = event.pos()
        if local.y() <= HEADER_H and (self._width - 22) <= local.x() <= self._width:
            self.toggle_collapsed()
            event.accept()
            return
        super().mouseDoubleClickEvent(event)

    def toggle_collapsed(self):
        self.node.collapsed = not self.node.collapsed
        self.rebuild()
        self.scene_ref.update_edges_for_node(self)
