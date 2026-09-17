# nodeeditor

A professional, embeddable visual node editor for **PySide6** — built for
no-code / visual-programming tools (think Blender shader nodes or UE5
Blueprints, but as a drop-in Python widget).

* Clean, minimal public API centered on a single `NodeWorkspace` object
* Typed, colored sockets (like Blender / UE5) with compatibility checking
* JSON serialization, ready to round-trip or feed a code generator
* Built-in Python code generation from the graph (topological order)
* Dark/light theming out of the box, every color controllable
* No high-contrast/neon colors — a muted, modern-but-casual palette
* Pan/zoom canvas, marquee select, draggable sockets, collapsible nodes,
  inline value widgets for unconnected inputs, searchable node palette,
  context menus, save/load

## Install

```bash
pip install PySide6
```

(This package itself has no dependencies beyond PySide6 — copy the
`nodeeditor/` folder into your project, or `pip install -e .` from here.)

## Quick start

```python
from nodeeditor import NodeWorkspace, NodeBase

workspace = NodeWorkspace(theme="dark")   # or theme="light", or a Theme(...)

@workspace.node("Math/Add", color="#5b8fb9")
class AddNode(NodeBase):
    inputs = [("A", "float", 0.0), ("B", "float", 0.0)]
    outputs = [("Result", "float")]

    def compute(self, a, b):
        return a + b

n1 = workspace.add_node("Math/Add", x=0, y=0)
n2 = workspace.add_node("Math/Add", x=280, y=0)
workspace.connect(n1.output_port("Result"), n2.input_port("A"))

workspace.show()   # opens a standalone window with palette + canvas
```

To embed inside your own `QMainWindow` / layout instead of opening a
standalone window:

```python
layout.addWidget(workspace.widget())
```

Run the full demo (math + text + branch nodes):

```bash
python examples/demo.py
```

## Core concepts

### Node types vs. node instances

You **register a node type** once (via `@workspace.node(...)` or
`workspace.register_node(key, MyNodeClass)`), then **place instances** of
it on the canvas with `workspace.add_node(key, x, y)`. Each instance keeps
its own port values and position.

```python
@workspace.node("Text/Concat", category="Text", color="#b98fc9")
class ConcatNode(NodeBase):
    inputs = [("A", "string", ""), ("B", "string", "")]
    outputs = [("Result", "string")]

    def compute(self, a, b):
        return a + b
```

* `inputs` / `outputs` are lists of `(name, type)` or `(name, type, default)`.
* `type` is any registered socket type name (`float`, `int`, `bool`,
  `string`, `vector`, `color`, `object`, `list`, `dict`, `exec`, `any`, or
  your own — see `register_socket_type`).
* `compute(self, **inputs)` is used for both `workspace.evaluate()` and as
  the reference implementation for code generation. Return a tuple if the
  node has more than one output (matching the order of `outputs`).

### Sockets & connections

```python
port_out = node_a.output_port("Result")
port_in  = node_b.input_port("A")
workspace.connect(port_out, port_in)
```

`connect()` validates that you're connecting an output to an input, that
the two socket types are compatible (`is_compatible_with`, with `"any"` as
a wildcard), and rejects self-loops. Cycles are caught at evaluation /
codegen time (`GraphError`).

### Evaluation

```python
results = workspace.evaluate()
# {node_id: {output_name: value}}
```

Runs every node's `compute()` once, in topological order, propagating
values along connections.

### Code generation

```python
print(workspace.generate_code(function_name="run_graph"))
```

Emits readable, standalone Python: one local variable per node, called in
dependency order, e.g.:

```python
def run_graph():
    """Auto-generated from NodeGraph. Do not hand-edit."""
    speed = _nodes['Math/Number'](value=12.5)
    multiply = _nodes['Math/Multiply'](a=speed, b=multiplier)
    add = _nodes['Math/Add'](a=multiply, b=10.0)
    return add
```

You provide a `_nodes` mapping from registry key to a plain callable
(typically `lambda **kw: MyNodeType().compute(**kw)`) — this keeps
generated code fully decoupled from the editor at runtime. Swap this
method out (or subclass `NodeGraph`) if you want to target another
language instead of Python.

### Serialization

```python
workspace.save("graph.json")
workspace.load("graph.json")

data = workspace.to_dict()   # plain JSON-ready dict
workspace.load_dict(data)
```

Format:

```json
{
  "nodes": [
    {"id": "node_1", "type": "Math/Add", "title": "Add", "x": 0, "y": 0,
     "collapsed": false, "inputs": {"A": 0.0, "B": 0.0}}
  ],
  "connections": [
    {"id": "conn_1", "from_node": "node_1", "from_socket": "Result",
     "to_node": "node_2", "to_socket": "A"}
  ]
}
```

### Theming

```python
from nodeeditor import Theme

workspace.set_theme("light")                      # built-in
workspace.set_theme(Theme(background="#202326"))   # fully custom
workspace.set_theme(workspace.theme.variant(accent="#c9a15b"))  # tweak one color
```

Every color used by the canvas, nodes, sockets, connections and side
panel is a field on `Theme` — see `nodeeditor/theme.py` for the full list
and the built-in `DARK_THEME` / `LIGHT_THEME` presets. Colors default to
muted blue-greys and soft accent tones rather than neon/high-contrast.

### Custom socket types

```python
from nodeeditor import register_socket_type
register_socket_type("event", color="#c9a15b", shape="triangle")
```

## Project layout

```
nodeeditor/
  theme.py          Theme dataclass + dark/light presets
  sockets.py         SocketType registry
  node.py             PortSpec / Port / NodeBase / Node / NodeType
  edge.py             Connection model
  registry.py        NodeRegistry (@workspace.node decorator target)
  graph.py            NodeGraph: mutation, validation, topo sort,
                       evaluate(), generate_python_code(), (de)serialization
  workspace.py        NodeWorkspace: the public-facing API + Qt window/widget
  graphics/
    socket_item.py    GraphicsSocket (QGraphicsItem)
    edge_item.py       GraphicsEdge (bezier QGraphicsPathItem)
    node_item.py        GraphicsNode (QGraphicsItem: header/body/sockets)
    scene.py            NodeScene (QGraphicsScene: grid, graph<->graphics sync)
    view.py              NodeView (QGraphicsView: pan/zoom/select/wiring)
    palette.py           NodePalette (searchable side panel)
examples/
  demo.py             End-to-end example
```

The logical model (`node.py`, `edge.py`, `graph.py`, `registry.py`) has
**no Qt dependency** and is unit-testable / usable headless (e.g. for
pure code generation in a build pipeline); the `graphics/` package is the
Qt presentation layer on top of it.

## Interaction reference

* **Left-drag** a socket to another socket to connect; drop on empty
  space to cancel.
* **Left-drag** empty canvas: marquee/rubber-band multi-select.
* **Middle-drag** or **space + drag**: pan.
* **Scroll wheel**: zoom (clamped 0.15x–3x).
* **Delete/Backspace**: delete selected nodes/connections.
* **F**: frame all nodes in view.
* **Double-click** a node's header collapse arrow: collapse/expand.
* **Right-click** canvas: add-node menu by category. **Right-click** a
  node: collapse/delete. **Right-click** a connection: delete.
* Palette panel: search, double-click a type to place it at the canvas
  center.
