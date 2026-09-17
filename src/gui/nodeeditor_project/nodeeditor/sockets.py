"""
Socket types: the typed, colored pins used for node inputs/outputs -
analogous to Blueprint pin colors or Blender socket colors.

Colors here are muted/casual rather than neon, but still clearly
distinguishable from one another.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class SocketType:
    name: str
    color: str = "#9aa1a8"
    shape: str = "circle"  # "circle" | "square" | "diamond" | "triangle"

    def is_compatible_with(self, other: "SocketType") -> bool:
        if self.name == other.name:
            return True
        # "any" is a wildcard socket type that accepts/produces anything
        return self.name == "any" or other.name == "any"


# A reasonably complete default palette covering common no-code data kinds.
SOCKET_TYPES = {
    "exec":   SocketType("exec",   color="#e8e8e8", shape="triangle"),
    "any":    SocketType("any",    color="#9aa1a8", shape="diamond"),
    "bool":   SocketType("bool",   color="#b06a6a", shape="circle"),
    "int":    SocketType("int",    color="#5f9ea3", shape="circle"),
    "float":  SocketType("float",  color="#6fae8a", shape="circle"),
    "string": SocketType("string", color="#b98fc9", shape="circle"),
    "vector": SocketType("vector", color="#c9a15b", shape="circle"),
    "color":  SocketType("color",  color="#c97fa1", shape="circle"),
    "object": SocketType("object", color="#5b8fb9", shape="square"),
    "list":   SocketType("list",   color="#8fa9c9", shape="square"),
    "dict":   SocketType("dict",   color="#a98fc9", shape="square"),
}


def register_socket_type(name: str, color: str = "#9aa1a8", shape: str = "circle") -> SocketType:
    """Register (or overwrite) a custom socket type and return it."""
    st = SocketType(name, color=color, shape=shape)
    SOCKET_TYPES[name] = st
    return st


def get_socket_type(name: str) -> SocketType:
    try:
        return SOCKET_TYPES[name]
    except KeyError:
        # Unknown types degrade gracefully to "any" styling rather than crashing,
        # so user typos don't take down the whole editor.
        return SocketType(name, color=SOCKET_TYPES["any"].color, shape="circle")
