"""Shared user-installed build tools; no editor-version dependency."""
from __future__ import annotations
import os
from pathlib import Path
from .settings import DataPaths


def FindCompiler(override: str = "", roots=()) -> Path | None:
    name = "clang++.exe" if os.name == "nt" else "clang++"
    if override:
        path = Path(override).expanduser()
        if path.is_dir():
            path = path / "bin" / name
        return path if path.is_file() else None  # Invalid overrides must not silently fall back.
    candidates = [DataPaths.Tools() / "llvm" / "bin" / name]
    candidates.extend(Path(root) / "toolchain" / "llvm" / "bin" / name for root in roots)
    return next((path for path in candidates if path.is_file()), None)
