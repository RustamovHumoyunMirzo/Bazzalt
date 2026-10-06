"""Verified downloads and transactional installs, independent of editor code."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import zipfile

from PySide6.QtCore import QThread, Signal
from bazzalt.settings import DataPaths, Version

DEFAULT_CATALOG = "https://github.com/RustamovHumoyunMirzo/Bazzalt/releases/latest/download/downloads.json"
MAX_CATALOG = 4 * 1024 * 1024
MAX_DOWNLOAD = 4 * 1024**3
MAX_EXPANDED = 12 * 1024**3


def _Https(url: str) -> str:
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("Downloads require an HTTPS URL without embedded credentials")
    return url


def FetchCatalog(url: str) -> list[dict]:
    with urlopen(Request(_Https(url), headers={"User-Agent": "BAZZALT-Hub/1.0"}), timeout=30) as response:
        _Https(response.geturl())
        data = response.read(MAX_CATALOG + 1)
    if len(data) > MAX_CATALOG: raise ValueError("Download catalog is too large")
    value = json.loads(data)
    if not isinstance(value, dict) or value.get("schema_version") != 1 or not isinstance(value.get("downloads"), list):
        raise ValueError("Unsupported download catalog")
    result = []
    for entry in value["downloads"]:
        ValidateEntry(entry)
        if entry["platform"] == "windows" and (entry["architecture"] == "x64" or entry["product"] == "core"):
            # x86 core SDKs are cross-build targets, not editor installations.
            if entry["product"] in {"editor","hub"} and entry["architecture"] != "x64":continue
            result.append(entry)
    return result


def ValidateEntry(entry: dict) -> None:
    if not isinstance(entry, dict): raise ValueError("Invalid download entry")
    if entry.get("product") not in {"editor", "core", "llvm", "hub", "windows-build-tools"}: raise ValueError("Unknown product")
    Version.Parse(entry.get("version", ""))
    if entry.get("platform") != "windows" or entry.get("architecture") not in {"x86", "x64"}: raise ValueError("Unsupported target")
    if not re.fullmatch(r"[a-zA-Z0-9_.-]+", entry.get("id", "")): raise ValueError("Unsafe download identifier")
    _Https(entry.get("url", ""))
    if not re.fullmatch(r"[a-fA-F0-9]{64}", entry.get("sha256", "")): raise ValueError("Missing SHA-256")
    if type(entry.get("size")) is not int or not 0 < entry["size"] <= MAX_DOWNLOAD: raise ValueError("Invalid download size")


def Download(entry: dict, target: Path, progress=lambda done, total: None, cancelled=lambda: False) -> None:
    ValidateEntry(entry)
    if entry["product"] in {"editor","hub"} and entry["architecture"] != "x64":
        raise ValueError("Hub and Editor require 64-bit")
    digest = hashlib.sha256(); total = 0
    with urlopen(Request(entry["url"], headers={"User-Agent": "BAZZALT-Hub/1.0"}), timeout=60) as response, target.open("xb") as output:
        _Https(response.geturl())
        while chunk := response.read(1024 * 1024):
            if cancelled(): raise InterruptedError("Download cancelled")
            total += len(chunk)
            if total > entry["size"]: raise ValueError("Download exceeds catalog size")
            digest.update(chunk); output.write(chunk); progress(total, entry["size"])
    if total != entry["size"] or digest.hexdigest().lower() != entry["sha256"].lower():
        raise ValueError("Downloaded package failed its size/SHA-256 check")


def Extract(archive: Path, target: Path) -> None:
    """Bounded extraction: reject traversal, Windows devices/ADS, links and collisions."""
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > 100000 or sum(item.file_size for item in entries) > MAX_EXPANDED:
            raise ValueError("Archive exceeds extraction limits")
        seen = set()
        for item in entries:
            name = item.filename.replace("\\", "/"); parts = PurePosixPath(name).parts
            if not parts or name.startswith("/") or any(part in {".", ".."} or ":" in part or part.rstrip(" .") != part or re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?", part) for part in parts):
                raise ValueError("Unsafe archive path")
            if stat.S_ISLNK(item.external_attr >> 16): raise ValueError("Archive links are not permitted")
            normalized = "/".join(parts).casefold()
            if normalized in seen: raise ValueError("Duplicate archive path")
            seen.add(normalized)
            destination = target.joinpath(*parts)
            if not destination.resolve().is_relative_to(target.resolve()): raise ValueError("Archive escaped installation directory")
            if item.is_dir(): destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with source.open(item) as stream, destination.open("xb") as output: shutil.copyfileobj(stream, output)


def _PEArchitecture(path: Path) -> str:
    with path.open("rb") as stream:
        if stream.read(2) != b"MZ": raise ValueError("Invalid executable")
        stream.seek(0x3c); offset = struct.unpack("<I", stream.read(4))[0]; stream.seek(offset)
        if stream.read(4) != b"PE\0\0": raise ValueError("Invalid executable")
        return {0x14c: "x86", 0x8664: "x64"}.get(struct.unpack("<H", stream.read(2))[0], "unsupported")


def Install(entry: dict, archive: Path, versions: Path) -> Path:
    ValidateEntry(entry)
    if entry["product"] in {"editor","hub"} and entry["architecture"] != "x64":
        raise ValueError("Hub and Editor require 64-bit")
    with archive.open("rb") as stream:
        digest=hashlib.file_digest(stream,"sha256").hexdigest()
    if archive.stat().st_size != entry["size"] or digest.lower() != entry["sha256"].lower():
        raise ValueError("Package changed or failed its integrity check before installation")
    product = entry["product"]
    if product not in {"editor", "llvm", "core"}: raise ValueError("This download is not an installable ZIP package")
    parent = versions if product == "editor" else DataPaths.Tools() if product == "llvm" else DataPaths.Root() / "SDKs"
    name = "bazzalt_" + entry["version"].replace(".", "_") if product == "editor" else "llvm" if product == "llvm" else entry["id"]
    parent.mkdir(parents=True, exist_ok=True); destination = parent / name
    if destination.exists(): raise ValueError("This version/tool is already installed; remove or relocate it before installing")
    with tempfile.TemporaryDirectory(prefix=".install-", dir=parent) as temporary:
        payload = Path(temporary) / "payload"; payload.mkdir(); Extract(archive, payload)
        if product == "editor":
            manifest = json.loads((payload / "editor.json").read_text(encoding="utf-8-sig"))
            if manifest.get("version") != entry["version"] or manifest.get("architecture") != entry["architecture"] or manifest.get("executable") != "Bazzalt.exe":
                raise ValueError("Editor manifest does not match download catalog")
            for required in ("Bazzalt.exe", "Editor/Bazzalt.dll", "Editor/bshad.dll", "ScriptSDK/lib/Bazzalt.lib", "ScriptSDK/include/Bazzalt/Scene.h"):
                if not (payload / required).is_file(): raise ValueError("Incomplete editor package: " + required)
            if not list((payload/"Editor").glob("_bazzalt_runtime*.pyd")):raise ValueError("Editor package has no native runtime")
        elif product == "llvm":
            if not (payload / "bin/clang++.exe").is_file(): raise ValueError("LLVM package lacks bin/clang++.exe")
        else:
            if not (payload / "include/Bazzalt/Scene.h").is_file() or not (payload / "lib/Bazzalt.dll").is_file(): raise ValueError("Incomplete Core SDK package")
        for path in payload.rglob("*"):
            if path.is_file() and path.suffix.lower() in {".exe", ".dll", ".pyd"} and _PEArchitecture(path) != entry["architecture"]:
                raise ValueError("Package contains a binary of the wrong architecture")
        payload.rename(destination)  # Same-filesystem atomic publication, no partial discovery.
    return destination


class DownloadWorker(QThread):
    Progress = Signal(int)
    Completed = Signal(str)
    Failed = Signal(str)
    CatalogLoaded = Signal(list)

    def __init__(self, parent=None, *, catalog_url=None, entry=None, versions=None):
        super().__init__(parent); self.CatalogUrl=catalog_url; self.Entry=entry; self.Versions=versions

    def run(self):
        try:
            if self.CatalogUrl:
                self.CatalogLoaded.emit(FetchCatalog(self.CatalogUrl)); return
            cache = DataPaths.Root() / "Downloads"; cache.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="download-", dir=cache) as temporary:
                archive = Path(temporary) / "package.zip"
                Download(self.Entry, archive, lambda done,total: self.Progress.emit(int(done*100/total)), self.isInterruptionRequested)
                if self.isInterruptionRequested(): raise InterruptedError("Download cancelled")
                self.Completed.emit(str(Install(self.Entry, archive, self.Versions)))
        except Exception as error: self.Failed.emit(str(error))
