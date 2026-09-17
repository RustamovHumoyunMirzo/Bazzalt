"""
NodeWorkspace: the single object most users interact with.

    workspace = NodeWorkspace(theme="dark")

    @workspace.node("Math/Add", color="#5b8fb9")
    class AddNode(NodeBase):
        inputs = [("A", "float", 0.0), ("B", "float", 0.0)]
        outputs = [("Result", "float")]
        def compute(self, a, b):
            return a + b

    n1 = workspace.add_node("Math/Add", x=0, y=0)
    n2 = workspace.add_node("Math/Add", x=280, y=0)
    workspace.connect(n1.output_port("Result"), n2.input_port("A"))

    workspace.show()  # opens a standalone window
    # or: layout.addWidget(workspace.widget())  to embed in your own app
"""

import sys
from typing import Callable, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QSplitter, QWidget, QVBoxLayout, QToolBar,
    QMenu, QFileDialog, QMessageBox,
)
from PySide6.QtGui import QAction

from .theme import Theme, resolve_theme
from .registry import NodeRegistry
from .graph import NodeGraph, GraphError
from .node import Node, Port
from .graphics import NodeScene, NodeView, GraphicsNode
from .graphics.palette import NodePalette


class NodeWorkspace:
    def __init__(self, theme="dark", registry: Optional[NodeRegistry] = None):
        self.theme: Theme = resolve_theme(theme)
        self.registry = registry or NodeRegistry()
        self.graph = NodeGraph(self.registry)

        self._app = None
        self._window = None
        self._widget = None
        self.scene: Optional[NodeScene] = None
        self.view: Optional[NodeView] = None
        self.palette: Optional[NodePalette] = None
        self._toolbar: Optional[QToolBar] = None

    # ------------------------------------------------------- registration

    def node(self, key: str, title: Optional[str] = None, color: Optional[str] = None,
              category: Optional[str] = None, description: str = "") -> Callable:
        """Decorator: @workspace.node("Category/Name") class MyNode(NodeBase): ..."""
        return self.registry.decorator(key, title=title, color=color,
                                        category=category, description=description)

    def register_node(self, key: str, impl: type, **kwargs):
        return self.registry.register(key, impl, **kwargs)

    # -------------------------------------------------------- graph editing

    def add_node(self, type_key: str, x: float = 0.0, y: float = 0.0,
                 title: Optional[str] = None) -> Node:
        return self.graph.add_node(type_key, x=x, y=y, title=title)

    def remove_node(self, node_id: str) -> None:
        self.graph.remove_node(node_id)

    def connect(self, output: Port, input: Port):
        return self.graph.connect(output, input)

    def clear(self) -> None:
        self.graph.clear()

    # ---------------------------------------------------------- evaluation

    def evaluate(self) -> dict:
        return self.graph.evaluate()

    def generate_code(self, function_name: str = "run_graph") -> str:
        return self.graph.generate_python_code(function_name=function_name)

    # -------------------------------------------------------- serialization

    def to_dict(self) -> dict:
        return self.graph.to_dict()

    def to_json(self, indent: int = 2) -> str:
        return self.graph.to_json(indent=indent)

    def load_dict(self, data: dict) -> None:
        self.graph.load_dict(data)

    def load_json(self, text: str) -> None:
        self.graph.load_json(text)

    def save(self, path: str) -> None:
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.to_json())

    def load(self, path: str) -> None:
        with open(path, "r", encoding="utf-8") as f:
            self.load_json(f.read())

    # -------------------------------------------------------------- theming

    def set_theme(self, theme) -> None:
        self.theme = resolve_theme(theme)
        if self.scene is not None:
            self.scene.apply_theme(self.theme)
        if self.palette is not None:
            self.palette.apply_theme(self.theme)
        if self._widget is not None:
            self._widget.setStyleSheet(f"background: {self.theme.background};")
        if self._toolbar is not None:
            self._toolbar.setStyleSheet(self._toolbar_stylesheet())

    # ---------------------------------------------------------------- Qt UI

    def _ensure_app(self):
        app = QApplication.instance()
        if app is None:
            app = QApplication(sys.argv)
        self._app = app
        return app

    def widget(self, with_palette: bool = True, with_toolbar: bool = True) -> QWidget:
        """Build (once) and return the embeddable editor widget: palette +
        canvas, ready to drop into any layout or window."""
        if self._widget is not None:
            return self._widget

        self._ensure_app()
        self.scene = NodeScene(self.graph, self.theme)
        self.view = NodeView(self.scene)
        self.view.node_context_requested.connect(self._on_node_context_menu)
        self.view.canvas_context_requested.connect(self._on_canvas_context_menu)

        container = QWidget()
        outer = QVBoxLayout(container)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        if with_toolbar:
            outer.addWidget(self._build_toolbar())

        if with_palette:
            splitter = QSplitter(Qt.Orientation.Horizontal)
            self.palette = NodePalette(self.registry, self.theme)
            self.palette.setMinimumWidth(200)
            self.palette.setMaximumWidth(340)
            self.palette.node_type_activated.connect(self._add_node_from_palette)
            splitter.addWidget(self.palette)
            splitter.addWidget(self.view)
            splitter.setStretchFactor(0, 0)
            splitter.setStretchFactor(1, 1)
            splitter.setSizes([240, 900])
            outer.addWidget(splitter)
        else:
            outer.addWidget(self.view)

        self._widget = container
        self.set_theme(self.theme)
        return container

    def _toolbar_stylesheet(self) -> str:
        return (
            f"QToolBar {{ background: {self.theme.panel_bg}; border-bottom: 1px solid {self.theme.panel_border}; "
            f"spacing: 6px; padding: 4px; }} QToolButton {{ color: {self.theme.node_body_text}; padding: 4px 8px; }}"
            f"QToolButton:hover {{ background: {self.theme.accent}33; border-radius: 4px; }}"
        )

    def _build_toolbar(self) -> QToolBar:
        bar = QToolBar()
        bar.setMovable(False)
        bar.setStyleSheet(self._toolbar_stylesheet())
        self._toolbar = bar

        def act(text, handler):
            a = QAction(text, bar)
            a.triggered.connect(handler)
            bar.addAction(a)
            return a

        act("Frame All", lambda: self.view._frame_all())
        act("Reset Zoom", lambda: self.view.reset_zoom())
        bar.addSeparator()
        act("Save\u2026", self._ui_save)
        act("Load\u2026", self._ui_load)
        bar.addSeparator()
        act("Generate Code", self._ui_generate_code)
        theme_toggle = act("Toggle Theme", self._ui_toggle_theme)
        return bar

    def _ui_toggle_theme(self):
        self.set_theme("light" if self.theme.name == "dark" else "dark")

    def _ui_save(self):
        path, _ = QFileDialog.getSaveFileName(self._widget, "Save Graph", "graph.json", "JSON (*.json)")
        if path:
            self.save(path)

    def _ui_load(self):
        path, _ = QFileDialog.getOpenFileName(self._widget, "Load Graph", "", "JSON (*.json)")
        if path:
            try:
                self.load(path)
            except Exception as exc:
                QMessageBox.warning(self._widget, "Load failed", str(exc))

    def _ui_generate_code(self):
        try:
            code = self.generate_code()
        except GraphError as exc:
            QMessageBox.warning(self._widget, "Cannot generate code", str(exc))
            return
        dlg = QMessageBox(self._widget)
        dlg.setWindowTitle("Generated Code")
        dlg.setText("<pre style='font-family: monospace; font-size: 11px;'>" +
                     code.replace("<", "&lt;").replace(">", "&gt;") + "</pre>")
        dlg.exec()

    def _add_node_from_palette(self, type_key: str):
        center = self.view.mapToScene(self.view.viewport().rect().center())
        self.add_node(type_key, x=center.x(), y=center.y())

    def _on_canvas_context_menu(self, scene_pos):
        menu = QMenu(self.view)
        for category, node_types in self.registry.categories().items():
            submenu = menu.addMenu(category)
            for nt in node_types:
                submenu_action = submenu.addAction(nt.title)
                submenu_action.triggered.connect(
                    lambda checked=False, key=nt.key, pos=scene_pos: self.add_node(
                        key, x=pos.x(), y=pos.y())
                )
        menu.exec(self.view.mapToGlobal(self.view.mapFromScene(scene_pos)))

    def _on_node_context_menu(self, graphics_node: GraphicsNode, scene_pos):
        menu = QMenu(self.view)
        collapse_action = menu.addAction("Collapse" if not graphics_node.node.collapsed else "Expand")
        delete_action = menu.addAction("Delete Node")
        chosen = menu.exec(self.view.mapToGlobal(self.view.mapFromScene(scene_pos)))
        if chosen is collapse_action:
            graphics_node.toggle_collapsed()
        elif chosen is delete_action:
            self.remove_node(graphics_node.node.id)

    # ------------------------------------------------------------ top-level

    def show(self, title: str = "Node Editor", width: int = 1280, height: int = 800):
        self._ensure_app()
        window = QMainWindow()
        window.setWindowTitle(title)
        window.resize(width, height)
        window.setCentralWidget(self.widget())
        window.show()
        self._window = window
        if QApplication.instance() is self._app and not hasattr(sys, "ps1"):
            self._app.exec()
        return window
