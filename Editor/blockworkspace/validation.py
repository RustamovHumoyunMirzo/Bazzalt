"""Document validation."""
from __future__ import annotations
from typing import Callable


class ValidationIssue:
    def __init__(self, BlockId: str, Message: str, Severity: str = "Warning"):
        self.BlockId, self.Message, self.Severity = BlockId, Message, Severity

    def __repr__(self):
        return f"{self.Severity}({self.BlockId}): {self.Message}"


def _coerce(block_id, item):
    if isinstance(item, ValidationIssue):
        return item
    if isinstance(item, tuple):
        return ValidationIssue(block_id, item[1], item[0])
    return ValidationIssue(block_id, str(item), "Warning")


def ValidateDocument(document, extra: list[Callable] | None = None) -> list:
    issues = []
    for b in document.Blocks.values():
        d = b.Definition
        if d.IsUnknown:
            issues.append(ValidationIssue(b.Id, f"Unknown block type '{d.TypeId}' (data preserved)", "Warning"))
            continue
        for ri in b.Inputs():
            if ri.Def.Required and ri.Kind == "value" and ri.Key not in b.Children and ri.Def.Shadow is None:
                issues.append(ValidationIssue(b.Id, f"'{ri.Def.Name}' needs a value", "Warning"))
            if ri.Kind == "field":
                msg = ri.Def.Validate(b.Values.get(ri.Key))
                if msg:
                    issues.append(ValidationIssue(b.Id, f"{ri.Def.Name}: {msg}", "Error"))
                elif ri.Def.Required and b.Values.get(ri.Key) in (None, ""):
                    issues.append(ValidationIssue(b.Id, f"'{ri.Def.Name}' is required", "Warning"))
        for m in d.Instance.Validate(b) or []:
            issues.append(_coerce(b.Id, m))
    for fn in extra or []:
        for m in fn(document) or []:
            if isinstance(m, ValidationIssue):
                issues.append(m)
    return issues
