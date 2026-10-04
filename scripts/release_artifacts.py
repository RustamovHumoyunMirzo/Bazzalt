"""Validate actual PE architecture, archive payloads, and emit Hub download entries."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

MACHINES = {0x014c: "x86", 0x8664: "x64", 0xaa64: "arm64"}


def Architecture(path: Path) -> str:
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ": raise ValueError(f"Not a Windows executable: {path}")
        stream.seek(0x3c)
        offset = struct.unpack("<I", stream.read(4))[0]
        stream.seek(offset)
        if stream.read(4) != b"PE\0\0": raise ValueError(f"Invalid PE header: {path}")
        machine = struct.unpack("<H", stream.read(2))[0]
        if machine not in MACHINES: raise ValueError(f"Unsupported PE machine {machine:#x}: {path}")
        return MACHINES[machine]


def Audit(root: Path, architecture: str, product: str) -> None:
    binaries = list(root.rglob("*.exe")) + list(root.rglob("*.dll")) + list(root.rglob("*.pyd"))
    if not binaries: raise ValueError("Payload has no Windows binaries")
    for path in binaries:
        if Architecture(path) != architecture: raise ValueError(f"Wrong architecture: {path}")
    if product == "hub":
        for path in root.rglob("*"):
            if path.name.lower() in {"versions", "scriptsdk", "bazzalt.dll", "bazzalt.exe", "toolchain"} or path.name.startswith("_bazzalt_runtime"):
                raise ValueError(f"Hub must not ship editors/core/tools: {path}")
    elif product == "editor":
        for required in ("Bazzalt.exe", "editor.json", "Editor/Bazzalt.dll", "ScriptSDK/include/Bazzalt/Scene.h", "ScriptSDK/lib/Bazzalt.lib"):
            if not (root / required).is_file(): raise ValueError(f"Missing editor file: {required}")
        if (root / "toolchain").exists(): raise ValueError("Editors must use separately installed build tools")


def Archive(root: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file(): archive.write(path, path.relative_to(root).as_posix())


def Entry(path: Path, product: str, version: str, architecture: str, base_url: str) -> dict:
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"id": f"{product}-{version}-windows-{architecture}", "product": product,
            "version": version, "platform": "windows", "architecture": architecture,
            "url": base_url.rstrip("/") + "/" + path.name, "sha256": digest,
            "size": path.stat().st_size, "filename": path.name}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--product", choices=("editor", "core", "hub", "llvm"), required=True)
    parser.add_argument("--architecture", choices=("x64", "x86"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base-url", default="")
    args = parser.parse_args()
    if args.root:
        Audit(args.root, args.architecture, args.product)
        Archive(args.root, args.output)
    if not args.output.is_file(): parser.error("Artifact does not exist")
    entry = Entry(args.output, args.product, args.version, args.architecture, args.base_url)
    args.output.with_suffix(args.output.suffix + ".json").write_text(json.dumps(entry, indent=2), encoding="utf-8")


if __name__ == "__main__": main()
