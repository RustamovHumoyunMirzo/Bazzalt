"""
Logical (non-graphical) node model.

A "node type" is a Python class deriving from NodeBase, registered on a
workspace via @workspace.node(...). Each placed instance in the graph is
a Node holding its own port values, position, and a reference back to its
NodeType definition.
"""

import itertools
from dataclasses import dataclass, field
from typing import Any, Callable, List, Optional, Tuple

from .sockets import get_socket_type

_id_counter = itertools.count(1)


def _next_id(prefix: str) -> str:
    return f"{prefix}_{next(_id_counter)}"


@dataclass
class PortSpec:
    """Declarative description of one input or output socket."""
    name: str
    type: str = "any"
    default: Any = None
    label: Optional[str] = None

    def display_label(self) -> str:
        return self.label if self.label is not None else self.name


@dataclass(eq=False)
class Port:
    """A concrete socket instance living on a placed Node."""
    node: "Node"
    spec: PortSpec
    is_input: bool
    index: int
    value: Any = None
    id: str = field(default_factory=lambda: _next_id("port"))

    @property
    def socket_type(self):
        return get_socket_type(self.spec.type)

    @property
    def name(self) -> str:
        return self.spec.name


class NodeBase:
    """
    Base class for user-defined node types.

    Subclass and set ``inputs`` / ``outputs`` as lists of tuples:
        (name, type)  or  (name, type, default)
    and optionally implement ``compute(self, **inputs) -> value | tuple``
    for evaluation/codegen. When there is more than one output, return a
    tuple matching the order of ``outputs``.

    Example:
        class AddNode(NodeBase):
            inputs = [("A", "float", 0.0), ("B", "float", 0.0)]
            outputs = [("Result", "float")]

            def compute(self, a, b):
                return a + b
    """

    inputs: List[tuple] = []
    outputs: List[tuple] = []
    category: str = "General"
    color: Optional[str] = None
    description: str = ""
    # If set, used verbatim as a Python source template for code generation
    # instead of calling compute() at runtime. Placeholders: {A}, {B}, ... by
    # input name (uppercased-safe identifiers), and result via assignment.
    code_template: Optional[str] = None

    def compute(self, **inputs):
        raise NotImplementedError(
            f"{type(self).__name__} does not implement compute(); "
            "override it or supply a code_template for codegen-only nodes."
        )


def normalize_port_specs(raw: List[tuple]) -> List[PortSpec]:
    specs = []
    for item in raw:
        if len(item) == 2:
            name, type_ = item
            default = None
        else:
            name, type_, default = item
        specs.append(PortSpec(name=name, type=type_, default=default))
    return specs


@dataclass
class NodeType:
    """Registered definition of a node type (the 'class', not an instance)."""
    key: str                       # unique registry key, e.g. "Math/Add"
    title: str                     # display title, e.g. "Add"
    category: str
    impl: type                     # the NodeBase subclass
    input_specs: List[PortSpec]
    output_specs: List[PortSpec]
    color: str = "#54595f"
    description: str = ""


class Node:
    """A placed instance of a NodeType inside a NodeGraph."""

    def __init__(self, node_type: NodeType, x: float = 0.0, y: float = 0.0,
                 node_id: Optional[str] = None, title: Optional[str] = None):
        self.id = node_id or _next_id("node")
        self.node_type = node_type
        self.title = title or node_type.title
        self.x = x
        self.y = y
        self.collapsed = False
        self.impl = node_type.impl()

        self.inputs: List[Port] = [
            Port(self, spec, True, i, value=spec.default)
            for i, spec in enumerate(node_type.input_specs)
        ]
        self.outputs: List[Port] = [
            Port(self, spec, False, i)
            for i, spec in enumerate(node_type.output_specs)
        ]

        # Graphics-layer item is attached lazily by the view; kept separate
        # so the logical model has no Qt dependency and is trivially testable
        # / serializable headless.
        self.graphics = None

    def input_port(self, name: str) -> Port:
        for p in self.inputs:
            if p.name == name:
                return p
        raise KeyError(f"Node '{self.title}' has no input '{name}'")

    def output_port(self, name: str) -> Port:
        for p in self.outputs:
            if p.name == name:
                return p
        raise KeyError(f"Node '{self.title}' has no output '{name}'")

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "type": self.node_type.key,
            "title": self.title,
            "x": self.x,
            "y": self.y,
            "collapsed": self.collapsed,
            "inputs": {p.name: p.value for p in self.inputs},
        }
