"""Persistent diagnostics for GUI launches, including native process failures."""
from __future__ import annotations
import faulthandler
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

_Log=None


def StartDiagnostics(directory:Path)->Path|None:
    global _Log
    if _Log is not None:return Path(_Log.name)
    path=Path(directory)/"editor-runtime.log"
    try:
        path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists():path.replace(path.with_name("editor-runtime.previous.log"))
        stream=path.open("ab",buffering=0)
        stream.write(f"Editor session {datetime.now(timezone.utc).isoformat()}\n".encode("utf-8"))
        # Redirect OS / CRT streams before loading the native runtime. Python
        # exceptions alone cannot report a Filament abort or access violation.
        for descriptor in (1,2):
            original=sys.stdout if descriptor==1 else sys.stderr
            if original is not None:
                try:original.flush()
                except (OSError,ValueError):pass
            os.dup2(stream.fileno(),descriptor)
        if os.name=="nt":
            import ctypes,msvcrt
            kernel=ctypes.WinDLL("kernel32",use_last_error=True)
            kernel.SetStdHandle.argtypes=[ctypes.c_uint32,ctypes.c_void_p]
            kernel.SetStdHandle.restype=ctypes.c_int
            handle=msvcrt.get_osfhandle(stream.fileno())
            for identifier in (-11,-12):kernel.SetStdHandle(identifier&0xffffffff,handle)
        if sys.stdout is None:sys.stdout=os.fdopen(os.dup(1),"w",encoding="utf-8",errors="backslashreplace",buffering=1)
        if sys.stderr is None:sys.stderr=os.fdopen(os.dup(2),"w",encoding="utf-8",errors="backslashreplace",buffering=1)
        faulthandler.enable(file=stream,all_threads=True)
        _Log=stream
        return path
    except (OSError,RuntimeError,ValueError):return None
