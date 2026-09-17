from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QLineEdit, QTreeWidget, QTreeWidgetItem, QLabel,
)

TYPE_KEY_ROLE = Qt.ItemDataRole.UserRole


class NodePalette(QWidget):
    """Left-hand panel listing registered node types by category. Double-click
    (or drag, handled by the caller) to place a node on the canvas."""

    node_type_activated = Signal(str)  # emits the type key, e.g. "Math/Add"

    def __init__(self, registry, theme, parent=None):
        super().__init__(parent)
        self.registry = registry
        self.theme = theme

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        title = QLabel("Nodes")
        title.setStyleSheet("font-weight: 600; font-size: 12px;")
        layout.addWidget(title)

        self.search = QLineEdit()
        self.search.setPlaceholderText("Search nodes\u2026")
        self.search.textChanged.connect(self.refresh)
        layout.addWidget(self.search)

        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.setDragEnabled(True)
        self.tree.itemActivated.connect(self._on_item_activated)
        layout.addWidget(self.tree)

        self.apply_theme(theme)
        self.refresh()

    def apply_theme(self, theme):
        self.theme = theme
        self.setStyleSheet(f"""
            QWidget {{ background: {theme.panel_bg}; color: {theme.node_body_text}; }}
            QLineEdit {{
                background: {theme.node_bg}; border: 1px solid {theme.panel_border};
                border-radius: 4px; padding: 4px 6px; color: {theme.node_body_text};
            }}
            QTreeWidget {{
                background: {theme.panel_bg}; border: none; color: {theme.node_body_text};
            }}
            QTreeWidget::item {{ padding: 3px 2px; border-radius: 3px; }}
            QTreeWidget::item:selected {{ background: {theme.accent}44; }}
        """)
        self.refresh()

    def refresh(self):
        query = self.search.text().strip().lower()
        self.tree.clear()
        for category, node_types in self.registry.categories().items():
            visible_types = [nt for nt in node_types if query in nt.title.lower() or not query]
            if not visible_types:
                continue
            cat_item = QTreeWidgetItem([category])
            cat_item.setFlags(cat_item.flags() & ~Qt.ItemFlag.ItemIsSelectable)
            font = cat_item.font(0)
            font.setBold(True)
            cat_item.setFont(0, font)
            cat_item.setForeground(0, QColor(self.theme.text_muted))
            self.tree.addTopLevelItem(cat_item)
            for nt in visible_types:
                child = QTreeWidgetItem([nt.title])
                child.setData(0, TYPE_KEY_ROLE, nt.key)
                child.setToolTip(0, nt.description or nt.key)
                child.setForeground(0, QColor(self.theme.node_body_text))
                cat_item.addChild(child)
            cat_item.setExpanded(True)

    def _on_item_activated(self, item, column):
        key = item.data(0, TYPE_KEY_ROLE)
        if key:
            self.node_type_activated.emit(key)
