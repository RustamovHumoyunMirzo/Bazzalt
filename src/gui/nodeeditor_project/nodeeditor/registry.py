"""
Registry of available node types, organized by category for the palette
panel, and used to instantiate placed Node objects and to deserialize
saved graphs.
"""

from typing import Callable, Dict, List, Optional

from .node import NodeBase, NodeType, normalize_port_specs


class NodeRegistry:
    def __init__(self):
        self._types: Dict[str, NodeType] = {}

    def register(self, key: str, impl: type, title: Optional[str] = None,
                  color: Optional[str] = None, category: Optional[str] = None,
                  description: str = "") -> NodeType:
        if not (isinstance(impl, type) and issubclass(impl, NodeBase)):
            raise TypeError("Registered node implementation must subclass NodeBase")

        cat, _, name = key.rpartition("/")
        category = category or (cat or impl.category or "General")
        display_title = title or (name or key)

        node_type = NodeType(
            key=key,
            title=display_title,
            category=category,
            impl=impl,
            input_specs=normalize_port_specs(getattr(impl, "inputs", [])),
            output_specs=normalize_port_specs(getattr(impl, "outputs", [])),
            color=color or impl.color or "#54595f",
            description=description or impl.description or "",
        )
        self._types[key] = node_type
        return node_type

    def decorator(self, key: str, title: Optional[str] = None, color: Optional[str] = None,
                   category: Optional[str] = None, description: str = "") -> Callable:
        def _wrap(cls: type) -> type:
            self.register(key, cls, title=title, color=color, category=category,
                           description=description)
            return cls
        return _wrap

    def get(self, key: str) -> NodeType:
        try:
            return self._types[key]
        except KeyError:
            raise KeyError(f"No node type registered under key '{key}'")

    def all(self) -> List[NodeType]:
        return list(self._types.values())

    def categories(self) -> Dict[str, List[NodeType]]:
        out: Dict[str, List[NodeType]] = {}
        for nt in self._types.values():
            out.setdefault(nt.category, []).append(nt)
        for group in out.values():
            group.sort(key=lambda nt: nt.title)
        return dict(sorted(out.items()))

    def __contains__(self, key: str) -> bool:
        return key in self._types
