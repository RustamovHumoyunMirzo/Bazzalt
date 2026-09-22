# Production packaging and installation

BAZZALT ships as two managed programs. `BazzaltHub` is the user entry point. The
editor is not launched or project-managed directly; every installed editor version
is a payload owned and selected by the Hub.

## Filesystem contract

The default project root uses Qt's platform Documents location:

```text
<Documents>/BazzaltProjects/
```

Shared mutable application data uses the platform's generic application-data
location and never the install directory:

```text
<AppData>/BAZZALT/data/Config/
<AppData>/BAZZALT/data/Logs/
```

Editor installations are immutable children of the Hub program directory:

```text
BazzaltHub.exe
versions/
  bazzalt_1_0_0/
    Bazzalt.exe
    editor.json
    Editor/
      _bazzalt_runtime.pyd
```

On Linux and macOS the executable suffix is omitted. `editor.json` declares the
semantic editor version, executable name, and maximum supported project format.
The Hub discovers this manifest on startup and offers only compatible editors for
a project. This separates the project format from product versions and provides
the compatibility boundary needed for future migrations.

## Building

Install Python runtime and build requirements, configure the CMake tree, then run:

```powershell
python -m pip install PySide6
python -m pip install -r requirements-build.txt
cmake -S . -B build -DBAZZALT_BUILD_EDITOR_BRIDGE=ON
./scripts/build_production.ps1 -Version 1.0.0
```

On Unix-like systems use `./scripts/build_production.sh 1.0.0`. Both scripts first
build the C++/pybind11 engine bridge, compile the Hub and editor with Nuitka, place
the editor beneath the Hub's version directory, and generate its manifest. The Hub
uses Qt WebEngine to render `Launcher/index.html`; Qt WebChannel exposes only the
project/version operations implemented by `HubBridge`.

The Hub build is a self-contained directory-mode distribution. This is preferable
for Qt WebEngine production deployment because its helper process and resources
remain installed once instead of being unpacked on every launch. The editor is a
managed version directory rather than an independently installed application.
