"""Runtime library inventory shared by native release staging tools.

CMake emits the inventory from registered targets. The fallback supports SDKs
created before that inventory was introduced.
"""

import json
from pathlib import Path


def LibraryNames(folder, platform):
    if platform not in ("windows", "linux", "macos", "android"):
        raise ValueError("Unsupported runtime library platform")
    folder = Path(folder)
    manifest = folder / "runtime-libraries.json"
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8-sig"))
        if (not isinstance(data, dict) or data.get("format_version") != 1
                or data.get("platform") != platform):
            raise ValueError("Runtime library inventory version or platform mismatch")
        names = data.get("libraries")
        if not isinstance(names, list) or not names or len(names) > 256:
            raise ValueError("Invalid runtime library inventory")
    else:
        modules = ("Bazzalt", "bazzalt_lua", "bazzalt_gui", "bazzalt_physics",
                   "bazzalt_ai", "bazzalt_network", "bazzalt_crypto")
        if platform == "windows":
            names = [name + ".dll" for name in modules]
        elif platform == "macos":
            names = ["lib" + name + ".dylib" for name in modules]
            names += sorted(path.name for path in folder.glob("libbazzalt_lua.*.dylib"))
        elif platform in ("linux", "android"):
            names = ["lib" + name + ".so" for name in modules]
            if platform == "linux":
                names += sorted(path.name for path in folder.glob("libbazzalt_lua.so.*"))
    for name in names:
        if (not isinstance(name, str) or not name or name in (".", "..")
                or any(character in name for character in '/\\:\x00')
                or not (name.endswith((".dll", ".dylib", ".so")) or ".so." in name)):
            raise ValueError("Unsafe runtime library name")
        if platform == "windows" and not name.endswith(".dll"):
            raise ValueError("Runtime library platform mismatch")
        if platform == "macos" and not name.endswith(".dylib"):
            raise ValueError("Runtime library platform mismatch")
        if platform in ("linux", "android") and not (name.endswith(".so") or ".so." in name):
            raise ValueError("Runtime library platform mismatch")
    return list(dict.fromkeys(names))
