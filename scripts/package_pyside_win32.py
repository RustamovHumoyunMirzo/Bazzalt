"""Package real CMake-installed Win32 bindings as conventional Python wheels."""
from __future__ import annotations
import argparse
import base64
import csv
import hashlib
import io
from pathlib import Path
import shutil
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from release_artifacts import Architecture


def Wheel(package: Path, output: Path, version: str, requires=()) -> Path:
    if not package.is_dir() or not (package / "__init__.py").is_file():
        raise ValueError(f"Missing installed Python package: {package}")
    binaries=[p for p in package.rglob("*") if p.suffix.lower() in {".dll", ".pyd", ".exe"}]
    if not binaries or any(Architecture(path) != "x86" for path in binaries):
        raise ValueError("Bindings must contain genuine Win32 binaries only")
    output.mkdir(parents=True,exist_ok=True)
    wheel=output/f"{package.name}-{version}-cp313-cp313-win32.whl"
    dist=f"{package.name}-{version}.dist-info"
    files={f"{package.name}/{path.relative_to(package).as_posix()}":path.read_bytes() for path in package.rglob("*") if path.is_file() and "__pycache__" not in path.parts}
    metadata=f"Metadata-Version: 2.1\nName: {package.name}\nVersion: {version}\nRequires-Python: >=3.13,<3.14\n"
    metadata+="".join(f"Requires-Dist: {requirement}\n" for requirement in requires)
    files[dist+"/METADATA"]=metadata.encode()
    files[dist+"/WHEEL"]=b"Wheel-Version: 1.0\nGenerator: Bazzalt-Win32-source-build\nRoot-Is-Purelib: false\nTag: cp313-cp313-win32\n"
    rows=[]
    for name,data in sorted(files.items()):
        digest=base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
        rows.append((name,"sha256="+digest,str(len(data))))
    rows.append((dist+"/RECORD","",""));record=io.StringIO();csv.writer(record,lineterminator="\n").writerows(rows);files[dist+"/RECORD"]=record.getvalue().encode()
    with zipfile.ZipFile(wheel,"w",zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
    return wheel


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--prefix",type=Path,required=True);parser.add_argument("--qt",type=Path,required=True);parser.add_argument("--source",type=Path,required=True);parser.add_argument("--output",type=Path,required=True);parser.add_argument("--version",required=True);args=parser.parse_args()
    site=args.prefix/"Lib/site-packages";pyside=site/"PySide6";shiboken=site/"shiboken6"
    for source in (args.qt/"bin",args.prefix/"bin"):
        for path in source.glob("*.dll"):
            destination=shiboken if path.name.lower().startswith("shiboken") else pyside
            shutil.copy2(path,destination/path.name)
    for executable in ("rcc.exe","uic.exe","lrelease.exe"):
        path=args.qt/"bin"/executable
        if path.is_file():shutil.copy2(path,pyside/path.name)
    shutil.copytree(args.qt/"plugins",pyside/"plugins",dirs_exist_ok=True)
    scripts=args.prefix/"bin/pyside_tool.py"
    if not scripts.is_file():raise ValueError("CMake installation did not include pyside_tool.py")
    (pyside/"scripts").mkdir(exist_ok=True)
    shutil.copy2(scripts,pyside/"scripts/pyside_tool.py")
    for package in (pyside,shiboken):
        shutil.copytree(args.source/"LICENSES",package/"licenses",dirs_exist_ok=True)
    print(Wheel(shiboken,args.output,args.version))
    print(Wheel(pyside,args.output,args.version,(f"shiboken6=={args.version}",)))


if __name__=="__main__":main()
