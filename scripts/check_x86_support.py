"""Produce evidence for Win32 prerequisites; never relabel an x64 payload."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--output",type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];versions=json.loads((root/"releases/versions.json").read_text())
    args.output.mkdir(parents=True,exist_ok=True)
    probe=subprocess.run([sys.executable,"-m","pip","download","--no-deps","--only-binary=:all:","--platform","win32","--python-version","313","--implementation","cp","--abi","cp313","--dest",str(args.output/"wheels"),f"PySide6=={versions['pyside']}"],capture_output=True,text=True)
    (args.output/"pyside-win32.log").write_text(probe.stdout+probe.stderr,encoding="utf-8")
    sdk=root/"deps/filament/lib/x86/md/filament.lib"
    report={"architecture":"x86","status":"blocked","pyside_win32_wheel_available":probe.returncode==0,"filament_win32_sdk_available":sdk.is_file(),
            "required_work":["Build and validate Qt 6, Shiboken, and PySide6 for Win32 (not an officially supported Windows target)","Build Filament v1.77.0 and its dependencies for Win32; retain x64 host shader tools separately","Run native, Python, renderer, and packaging tests with a real 32-bit Python runtime"],
            "note":"This is a support report, NOT an executable or a completed Win32 port.",
            "sources":["https://doc.qt.io/qt-6.10/supported-platforms.html","https://pypi.org/project/PySide6/","https://github.com/google/filament/blob/v1.77.0/BUILDING.md"]}
    (args.output/"support.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__=="__main__":main()
