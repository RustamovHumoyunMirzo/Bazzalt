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
uses native Qt Widgets and a signal-based controller for project/version
operations. It does not load HTML, WebEngine, WebChannel, or editor widgets.

## Compiled application resources

Production builds no longer copy `Editor/assets`, `Launcher/index.html`, or
`Launcher/BazzaltLogo.svg` into the installation. Before Nuitka runs,
`scripts/compile_resources.py` invokes PySide6's Qt resource compiler and creates
three generated Python resource modules: editor assets, Hub branding, and
shared branding. Nuitka includes these modules as compiled code; the resource
byte arrays live in the programs rather than as raw SVG, JSON, or GLB files.
Generated modules are ignored by Git. Regenerate them on every production build
so changed icons, locales, layouts, and future assets are included automatically.

```powershell
python scripts/compile_resources.py
python -m unittest Editor.tests.test_compiled_resources
python scripts/compile_resources.py --audit dist/production/bazzalt_hub.dist
```

Qt resolves editor assets through `:/bazzalt/editor/...`, Hub branding through
`:/bazzalt/hub/BazzaltLogo.svg`, and shared branding through
`:/bazzalt/branding/BazzaltLogo.svg`. The Hub no longer contains a web page. Theme
stylesheet icons also use the resource service, not installation file paths.
The original docking icons remain in their existing compiled Qt package.

`ResourceManager.Path()` returns a Qt-compatible filename. `ReadBytes()` and
`ReadText()` read either source files or compiled resources; `Icon()` and
`Pixmap()` work in both modes. `Resolve()` preserves its Path return value in
source mode and returns a Qt resource string in compiled mode. Explicit custom
resource roots remain file-based for development and testing. Source launches
continue using editable files without regeneration. Set
`BAZZALT_COMPILED_RESOURCES=1` only to test compiled-resource loading from source.
Production never falls back to raw files if its generated resource module is
missing: it reports a repair/rebuild error.

Distribution and installer audits reject raw application resources, including
stale resources inside managed editor-version directories. Use a clean output
directory if an older distribution fails this check. The audit does not delete
anything. Public C++ SDK headers, editor manifests, mutable user settings,
project assets, and Qt/Chromium's vendor runtime files remain ordinary files;
they are not immutable Bazzalt UI resources.

Embedding is packaging, not encryption: someone inspecting the executable can
extract resource data. Do not store credentials or secrets in these resources.

The Hub build is a self-contained directory-mode distribution using Qt Widgets;
Chromium/WebEngine helper processes are not part of the Hub. The editor is a
managed version directory rather than an independently installed application.

## Windows installer

Inno Setup 6 turns the complete Nuitka distribution—including the locally bundled
editor and engine bridge—into one offline `BazzaltHub-Setup.exe`:

```powershell
winget install JRSoftware.InnoSetup
./scripts/build_installer.ps1 -Version 1.0.0
```

To compile the applications and installer in one operation, use:

```powershell
./scripts/build_production.ps1 -Version 1.0.0 -CreateInstaller
```

The default installation is `{autopf}\BAZZALT Hub`. Editor versions remain under
`{app}\versions\bazzalt_1_0_0`, matching runtime discovery. An upgrade cleans and
replaces only the editor version included by that installer; other installed
versions remain available. Uninstall removes program files and shortcuts but
deliberately preserves projects and user data.

The build wrapper supports `-InnoCompiler` for CI agents with a nonstandard Inno
Setup location. Code signing is intentionally not faked: production releases must
sign the finished installer with an organization-owned certificate in the release
pipeline.
