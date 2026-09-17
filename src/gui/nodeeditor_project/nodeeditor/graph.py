"""
NodeGraph: the logical (Qt-free) graph model. Owns nodes and connections,
handles validation, serialization to/from plain dicts (JSON-ready), and
topological evaluation / Python code generation.
"""

import json
import keyword
import re
from typing import Callable, Dict, List, Optional

from .edge import Connection
from .node import Node, Port
from .registry import NodeRegistry


class GraphError(Exception):
    pass


class NodeGraph:
    def __init__(self, registry: NodeRegistry):
        self.registry = registry
        self.nodes: Dict[str, Node] = {}
        self.connections: List[Connection] = []
        self._listeners: List[Callable[[str, object], None]] = []

    # ---- change notification (for the graphics scene to stay in sync) ----

    def on_change(self, callback: Callable[[str, object], None]) -> None:
        self._listeners.append(callback)

    def _emit(self, event: str, payload=None) -> None:
        for cb in self._listeners:
            cb(event, payload)

    # ---------------------------- mutation ---------------------------------

    def add_node(self, type_key: str, x: float = 0.0, y: float = 0.0,
                 title: Optional[str] = None, node_id: Optional[str] = None) -> Node:
        node_type = self.registry.get(type_key)
        node = Node(node_type, x=x, y=y, node_id=node_id, title=title)
        self.nodes[node.id] = node
        self._emit("node_added", node)
        return node

    def remove_node(self, node_id: str) -> None:
        node = self.nodes.get(node_id)
        if node is None:
            return
        for conn in self.connections_of(node):
            self.remove_connection(conn.id)
        del self.nodes[node_id]
        self._emit("node_removed", node)

    def connect(self, output: Port, input: Port, *, replace_existing: bool = True) -> Connection:
        if output.is_input or not input.is_input:
            raise GraphError("connect(output, input) requires an output port and an input port")
        if output.node is input.node:
            raise GraphError("Cannot connect a node to itself")
        if not output.socket_type.is_compatible_with(input.socket_type):
            raise GraphError(
                f"Incompatible socket types: {output.socket_type.name} -> {input.socket_type.name}"
            )

        if replace_existing:
            for existing in self.connections_to(input):
                self.remove_connection(existing.id)

        conn = Connection(output, input)
        self.connections.append(conn)
        self._emit("connection_added", conn)
        return conn

    def remove_connection(self, connection_id: str) -> None:
        conn = next((c for c in self.connections if c.id == connection_id), None)
        if conn is None:
            return
        self.connections.remove(conn)
        self._emit("connection_removed", conn)

    def connections_of(self, node: Node) -> List[Connection]:
        return [c for c in self.connections
                if c.output_port.node is node or c.input_port.node is node]

    def connections_to(self, port: Port) -> List[Connection]:
        return [c for c in self.connections if c.input_port is port]

    def connections_from(self, port: Port) -> List[Connection]:
        return [c for c in self.connections if c.output_port is port]

    def clear(self) -> None:
        self.nodes.clear()
        self.connections.clear()
        self._emit("cleared", None)

    # ------------------------------ ordering --------------------------------

    def topological_order(self) -> List[Node]:
        """Return nodes ordered so every node's dependencies precede it.
        Raises GraphError on a cycle."""
        indegree = {nid: 0 for nid in self.nodes}
        deps: Dict[str, List[str]] = {nid: [] for nid in self.nodes}
        for c in self.connections:
            src, dst = c.output_port.node.id, c.input_port.node.id
            deps[src].append(dst)
            indegree[dst] += 1

        ready = [nid for nid, d in indegree.items() if d == 0]
        ready.sort()  # deterministic ordering for reproducible codegen
        order: List[str] = []
        while ready:
            nid = ready.pop(0)
            order.append(nid)
            for nxt in deps[nid]:
                indegree[nxt] -= 1
                if indegree[nxt] == 0:
                    ready.append(nxt)
            ready.sort()

        if len(order) != len(self.nodes):
            raise GraphError("Graph contains a cycle; cannot produce a topological order")
        return [self.nodes[nid] for nid in order]

    # ------------------------------ evaluation -------------------------------

    def evaluate(self) -> Dict[str, dict]:
        """Run every node's compute() in dependency order and propagate values
        along connections. Returns {node_id: {output_name: value}}."""
        results: Dict[str, dict] = {}
        for node in self.topological_order():
            for c in self.connections:
                if c.input_port.node is node:
                    src_results = results.get(c.output_port.node.id, {})
                    c.input_port.value = src_results.get(c.output_port.name)

            kwargs = {_safe_ident(p.name): p.value for p in node.inputs}
            if node.outputs:
                raw = node.impl.compute(**kwargs)
                values = raw if isinstance(raw, tuple) else (raw,)
                results[node.id] = {
                    out.name: val for out, val in zip(node.outputs, values)
                }
            else:
                node.impl.compute(**kwargs)
                results[node.id] = {}
        return results

    # ------------------------------ codegen ----------------------------------

    def generate_python_code(self, function_name: str = "run_graph") -> str:
        """Generate readable, standalone Python source implementing the graph,
        by inlining each node's compute() call in topological order."""
        order = self.topological_order()
        var_names: Dict[str, str] = {}
        lines = [f"def {function_name}():", '    """Auto-generated from NodeGraph. Do not hand-edit."""']

        if not order:
            lines.append("    pass")
            return "\n".join(lines) + "\n"

        for node in order:
            var_base = _safe_ident(node.title) or _safe_ident(node.node_type.key.split("/")[-1])
            call_args = []
            for p in node.inputs:
                if self.connections_to(p):
                    src_conn = self.connections_to(p)[0]
                    src_var = var_names[src_conn.output_port.node.id]
                    if len(src_conn.output_port.node.outputs) > 1:
                        idx = src_conn.output_port.index
                        arg_val = f"{src_var}[{idx}]"
                    else:
                        arg_val = src_var
                else:
                    arg_val = repr(p.value)
                call_args.append(f"{_safe_ident(p.name)}={arg_val}")

            impl_cls = node.node_type.impl
            call = f"_nodes['{node.node_type.key}']({', '.join(call_args)})"
            if node.outputs:
                var = _unique_ident(var_base, var_names.values())
                var_names[node.id] = var
                lines.append(f"    {var} = {call}")
            else:
                lines.append(f"    {call}")

        if order[-1].outputs:
            lines.append(f"    return {var_names[order[-1].id]}")
        lines.append("")
        header = (
            "# _nodes maps registry keys to callables; wire this up to your\n"
            "# real node implementations (node_type.impl().compute) before running.\n"
        )
        return header + "\n".join(lines)

    # --------------------------- serialization -------------------------------

    def to_dict(self) -> dict:
        return {
            "nodes": [n.to_dict() for n in self.nodes.values()],
            "connections": [c.to_dict() for c in self.connections],
        }

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    def load_dict(self, data: dict) -> None:
        self.clear()
        for nd in data.get("nodes", []):
            node = self.add_node(nd["type"], x=nd.get("x", 0.0), y=nd.get("y", 0.0),
                                  title=nd.get("title"), node_id=nd.get("id"))
            node.collapsed = nd.get("collapsed", False)
            for name, value in nd.get("inputs", {}).items():
                try:
                    node.input_port(name).value = value
                except KeyError:
                    pass
        for cd in data.get("connections", []):
            try:
                out_node = self.nodes[cd["from_node"]]
                in_node = self.nodes[cd["to_node"]]
                self.connect(out_node.output_port(cd["from_socket"]),
                             in_node.input_port(cd["to_socket"]))
            except (KeyError, GraphError):
                continue

    def load_json(self, text: str) -> None:
        self.load_dict(json.loads(text))


def _safe_ident(name: str) -> str:
    ident = re.sub(r"\W|^(?=\d)", "_", name).lower() or "value"
    if keyword.iskeyword(ident):
        ident += "_"
    return ident


def _unique_ident(base: str, existing) -> str:
    existing = set(existing)
    if base not in existing:
        return base
    i = 2
    while f"{base}_{i}" in existing:
        i += 1
    return f"{base}_{i}"
