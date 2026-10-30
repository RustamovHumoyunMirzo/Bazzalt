"""Shared user-installed build tools; no editor-version dependency."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
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
    found=next((path for path in candidates if path.is_file()),None)
    if found is None and os.name!="nt":
        system=shutil.which("clang++");found=Path(system) if system else None
    return found


def FindRuntime(platform: str,architecture: str,app_root: Path,core_version: str) -> Path:
    """Prefer the editor template, then separately installed matching Core SDKs."""
    import json
    name=platform+'-'+architecture;app_root=Path(app_root)
    candidates=[app_root/'RuntimeTemplates'/name,Path(__file__).resolve().parents[1]/'build/RuntimeTemplates'/name]
    base=DataPaths.Root()/'SDKs'
    if base.is_dir():candidates.extend(sorted(base.glob('*/RuntimeTemplates/'+name)))
    for path in candidates:
        try:
            record=json.loads((path/'runtime.json').read_text(encoding='utf-8-sig'))
            if record.get('platform')==platform and record.get('architecture')==architecture and record.get('core_version')==core_version:return path
        except (OSError,ValueError):continue
    return candidates[0]
