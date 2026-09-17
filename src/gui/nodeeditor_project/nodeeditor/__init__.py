"""
nodeeditor - A professional, embeddable visual node editor for PySide6.

Public API:

    from nodeeditor import NodeWorkspace, NodeBase, SocketType, Theme

    workspace = NodeWorkspace(theme="dark")

    @workspace.node("Math/Add", color="#5b8fb9")
    class AddNode(NodeBase):
        inputs = [("A", "float", 0.0), ("B", "float", 0.0)]
        outputs = [("Result", "float")]

        def compute(self, a, b):
            return a + b

    workspace.show()

See examples/demo.py for a complete walkthrough.
"""

from .theme import Theme, DARK_THEME, LIGHT_THEME
from .sockets import SocketType, SOCKET_TYPES, register_socket_type
from .node import NodeBase, PortSpec
from .edge import Connection
from .graph import NodeGraph
from .registry import NodeRegistry
from .workspace import NodeWorkspace

__all__ = [
    "NodeWorkspace",
    "NodeBase",
    "PortSpec",
    "Connection",
    "NodeGraph",
    "NodeRegistry",
    "Theme",
    "DARK_THEME",
    "LIGHT_THEME",
    "SocketType",
    "SOCKET_TYPES",
    "register_socket_type",
]

__version__ = "0.1.0"
