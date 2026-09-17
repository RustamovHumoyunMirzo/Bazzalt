"""
Demo: a small "no-code" math/string graph.

Run with:
    python examples/demo.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from nodeeditor import NodeWorkspace, NodeBase


def build_workspace() -> NodeWorkspace:
    workspace = NodeWorkspace(theme="dark")

    @workspace.node("Math/Number", color="#6fae8a", category="Math")
    class NumberNode(NodeBase):
        outputs = [("Value", "float")]
        inputs = [("Value", "float", 0.0)]

        def compute(self, value):
            return value

    @workspace.node("Math/Add", color="#6fae8a")
    class AddNode(NodeBase):
        inputs = [("A", "float", 0.0), ("B", "float", 0.0)]
        outputs = [("Result", "float")]

        def compute(self, a, b):
            return a + b

    @workspace.node("Math/Multiply", color="#6fae8a")
    class MultiplyNode(NodeBase):
        inputs = [("A", "float", 1.0), ("B", "float", 1.0)]
        outputs = [("Result", "float")]

        def compute(self, a, b):
            return a * b

    @workspace.node("Text/Concat", color="#b98fc9", category="Text")
    class ConcatNode(NodeBase):
        inputs = [("A", "string", ""), ("B", "string", "")]
        outputs = [("Result", "string")]

        def compute(self, a, b):
            return f"{a}{b}"

    @workspace.node("Text/Print", color="#b98fc9", category="Text")
    class PrintNode(NodeBase):
        inputs = [("Value", "any", None)]
        outputs = []

        def compute(self, value):
            print(f"[Print] {value}")

    @workspace.node("Logic/Branch", color="#b06a6a", category="Logic")
    class BranchNode(NodeBase):
        inputs = [("Condition", "bool", False), ("A", "any", None), ("B", "any", None)]
        outputs = [("Result", "any")]

        def compute(self, condition, a, b):
            return a if condition else b

    a = workspace.add_node("Math/Number", x=-320, y=-80, title="Speed")
    a.input_port("Value").value = 12.5
    b = workspace.add_node("Math/Number", x=-320, y=80, title="Multiplier")
    b.input_port("Value").value = 2.0

    mul = workspace.add_node("Math/Multiply", x=-40, y=0)
    add = workspace.add_node("Math/Add", x=240, y=0)
    add.input_port("B").value = 10.0

    concat = workspace.add_node("Text/Concat", x=-40, y=220)
    concat.input_port("A").value = "Result: "
    printer = workspace.add_node("Text/Print", x=520, y=110)

    workspace.connect(a.output_port("Value"), mul.input_port("A"))
    workspace.connect(b.output_port("Value"), mul.input_port("B"))
    workspace.connect(mul.output_port("Result"), add.input_port("A"))
    workspace.connect(add.output_port("Result"), printer.input_port("Value"))

    return workspace


if __name__ == "__main__":
    ws = build_workspace()

    print("--- evaluate() ---")
    print(ws.evaluate())

    print("\n--- generate_code() ---")
    print(ws.generate_code())

    print("\n--- to_json() (first 300 chars) ---")
    print(ws.to_json()[:300])

    ws.show(title="No-Code Node Editor \u2014 Demo")
