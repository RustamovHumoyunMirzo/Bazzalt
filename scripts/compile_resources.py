"""Compile immutable application assets into Python Qt resource modules for Nuitka."""
from __future__ import annotations
import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def Compile(root: Path = ROOT, build: Path | None = None) -> list[Path]:
    root = root.resolve()
    build = (build or root / "build/CompiledResources").resolve()
    build.mkdir(parents=True, exist_ok=True)
    assets = root / "Editor/assets"
    files = sorted(path for path in assets.rglob("*") if path.is_file())
    if not files: raise RuntimeError("Editor assets are missing")
    packages = {
        "editor": [(path.relative_to(assets).as_posix(), path) for path in files],
        "hub": [(name, root / "Launcher" / name) for name in ("index.html", "BazzaltLogo.svg")],
        "branding": [("BazzaltLogo.svg", root / "Launcher/BazzaltLogo.svg")],
    }
    outputs = []
    for name, entries in packages.items():
        document = ET.Element("RCC")
        resource = ET.SubElement(document, "qresource", prefix=f"/bazzalt/{name}")
        for alias, path in entries:
            if not path.is_file(): raise FileNotFoundError(path)
            if not path.resolve().is_relative_to(root): raise ValueError(f"Asset escapes source tree: {path}")
            ET.SubElement(resource, "file", alias=alias).text = path.as_posix()
        qrc = build / f"{name}.qrc"
        ET.ElementTree(document).write(qrc, encoding="utf-8", xml_declaration=True)
        output = root / "bazzalt" / f"_{name}_resources_rc.py"
        subprocess.run([sys.executable, "-c", "from PySide6.scripts.pyside_tool import rcc; rcc()",
                        str(qrc), "-o", str(output)], check=True, cwd=root)
        outputs.append(output)
    return outputs


def Audit(directory: Path) -> None:
    """Fail closed if a production distribution still contains source UI resources."""
    if not directory.is_dir(): raise FileNotFoundError(directory)
    leaked = []
    for path in directory.rglob("*"):
        suffix = tuple(part.casefold() for part in path.relative_to(directory).parts[-2:])
        if suffix in (("editor", "assets"), ("launcher", "index.html"), ("launcher", "bazzaltlogo.svg")):
            leaked.append(str(path))
    if leaked: raise RuntimeError("Raw application resources leaked into production: " + ", ".join(leaked))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-directory", type=Path)
    parser.add_argument("--audit", type=Path)
    args = parser.parse_args()
    if args.audit: Audit(args.audit.resolve())
    else:
        for output in Compile(build=args.build_directory): print(f"Compiled resource module: {output}")
