"""Print native library filenames for PowerShell release tools."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bazzalt.runtime_libraries import LibraryNames

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", type=Path, required=True)
    parser.add_argument("--platform", choices=("windows", "linux", "macos", "android"), required=True)
    args = parser.parse_args()
    print("\n".join(LibraryNames(args.folder, args.platform)))
