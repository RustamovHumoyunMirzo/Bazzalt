"""Stage upstream license texts without importing GUI modules or downloading code."""
from __future__ import annotations
import argparse
import importlib.metadata
import json
from pathlib import Path
import shutil
import sys

SOURCES = {
    'Jolt.txt': 'deps/jolt/LICENSE',
    'EnTT.txt': 'deps/entt/LICENSE',
    'Filament.txt': 'deps/filament/LICENSE',
    'rapidyaml.txt': 'deps/rapidyaml/LICENSE.txt',
    'c4core.txt': 'deps/rapidyaml/ext/c4core/LICENSE.txt',
    'Lua.txt': 'deps/lua/README',
    'Sol2.txt': 'deps/sol2/LICENSE.txt',
    'SDL3.txt': 'deps/SDL3/LICENSE.txt',
    'miniaudio.txt': 'deps/miniaudio/LICENSE',
    'stb_vorbis-source.txt': 'deps/miniaudio/extras/stb_vorbis.c',
    'FreeType.txt': 'deps/freetype/docs/FTL.TXT',
    'FreeType-zlib.txt': 'deps/freetype/src/gzip/zlib.h',
    'HarfBuzz.txt': 'deps/harfbuzz/COPYING',
    'SheenBidi.txt': 'deps/sheenbidi/LICENSE',
}
PYTHON_PACKAGES = ('PySide6', 'PySide6-Essentials', 'PySide6-Addons', 'shiboken6',
                   'PyYAML', 'cryptography', 'cffi', 'pycparser', 'zstandard',
                   'Nuitka', 'ordered-set', 'setuptools', 'packaging', 'pybind11')

def Collect(destination, root=None, python=False):
    root = Path(root or Path(__file__).resolve().parents[1]).resolve()
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    inventory = {}
    if (root/'licenses/third_party').is_dir():shutil.copytree(root/'licenses/third_party',destination/'upstream',dirs_exist_ok=True)
    for name, relative in SOURCES.items():
        source = root / relative
        if source.is_file():
            shutil.copyfile(source, destination / name)
            inventory[name] = relative
    # A source-built SDK may supply additional dependency NOTICE/LICENSE files.
    # Preserve their directory structure instead of replacing them with our table.
    for folder in ('deps/filament/licenses', 'deps/filament/third_party_licenses', 'deps/filament/NOTICE'):
        source = root / folder
        if source.is_dir(): shutil.copytree(source, destination/'Filament-notices'/source.name, dirs_exist_ok=True)
        elif source.is_file(): shutil.copyfile(source, destination/'Filament-NOTICE.txt')
    if (root/'THIRD_PARTY.md').is_file(): shutil.copyfile(root/'THIRD_PARTY.md', destination/'THIRD_PARTY.md')
    if python:
        for component in ('Editor','Launcher'):
            if (root/component/'LICENSE').is_file():shutil.copyfile(root/component/'LICENSE',destination/f'Bazzalt-{component}.txt')
        for package in PYTHON_PACKAGES:
            try: distribution = importlib.metadata.distribution(package)
            except importlib.metadata.PackageNotFoundError: continue
            copied = []
            for entry in distribution.files or ():
                if not any(word in str(entry).lower() for word in ('license', 'copying', 'notice')): continue
                source = distribution.locate_file(entry)
                if not source.is_file() or source.suffix in ('.py', '.pyc', '.pyd', '.dll'): continue
                target = destination/'python'/package/f'{len(copied):03}-{source.name}'
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target);copied.append(target.relative_to(destination).as_posix())
            inventory[package] = dict(version=distribution.version, notices=copied)
        for source in (Path(sys.base_prefix)/'LICENSE.txt', Path(sys.base_prefix)/'LICENSE'):
            if source.is_file(): shutil.copyfile(source, destination/'Python.txt');break
    (destination/'inventory.json').write_text(json.dumps(inventory, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    return inventory

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--python',action='store_true',help='Include notices from installed Python distributions')
    args=parser.parse_args();Collect(args.output,python=args.python)
