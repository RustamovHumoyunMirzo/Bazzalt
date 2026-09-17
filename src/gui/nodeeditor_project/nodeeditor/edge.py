"""Logical connection between an output Port and an input Port."""

import itertools
from dataclasses import dataclass, field
from typing import Optional

from .node import Port

_id_counter = itertools.count(1)


@dataclass(eq=False)
class Connection:
    output_port: Port
    input_port: Port
    id: str = field(default_factory=lambda: f"conn_{next(_id_counter)}")
    graphics: Optional[object] = field(default=None, repr=False, compare=False)

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "from_node": self.output_port.node.id,
            "from_socket": self.output_port.name,
            "to_node": self.input_port.node.id,
            "to_socket": self.input_port.name,
        }
