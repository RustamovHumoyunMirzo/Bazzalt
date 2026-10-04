# Production packaging and installation

Windows releases are three independent products: Editor and Core **0.5.0**,
and Hub **1.0.0**. Versions and dependency selections are in
[releases/versions.json](../../releases/versions.json). Hub installers never
contain an editor, engine DLL, SDK, or compiler.

## Storage and lifecycle

Projects default to Qt's Documents location, `BazzaltProjects/`.
Mutable settings, logs, downloads, and tools live under the platform generic
application-data location, `BAZZALT/data/`. Shared LLVM is installed at
`Tools/llvm/bin/clang++.exe`, not inside any editor version.

Existing program-directory `versions/` installations remain discoverable.
New Hub downloads default to writable `data/Editors/` (saved as the Hub's
versions-root preference). This avoids requiring administrator rights to install
an editor beneath Program Files. Locate Editors can select a different root.

An editor ZIP expands to:

```text
bazzalt_0_5_0/
  Bazzalt.exe
  editor.json
  Editor/
    _bazzalt_runtime.cp313-win_amd64.pyd
    Bazzalt.dll
    bshad.dll
  ScriptSDK/
    include/Bazzalt/...
    include/entt/...
    lib/Bazzalt.dll
    lib/Bazzalt.lib
  tools/filament/{matc,cmgen,filamesh}.exe
```

Core ZIPs contain public headers, EnTT headers, Release DLL/import library,
license, and `core.json`. Debug libraries and private Runtime headers are not
staged. Filament import tools stay with Editor because imports require the exact
renderer version; gameplay LLVM is downloaded independently. Source-development
Hub registrations preserve the previous unreleased 1.0.0 compatibility placeholder
so existing development projects are not silently downgraded. Shipped editor
versions are read from their manifests.

## Local Windows builds

Install Python 3.13 x64, PySide6 6.10.2, pybind11, requirements-build.txt,
Visual Studio C++ Build Tools with Windows SDK, and Inno Setup 6 (Hub only).
Fetch pinned dependencies using the existing get_entt, get_rapidyaml, get_sdl3,
and get_filament PowerShell scripts, then configure CMake:

```powershell
cmake -S . -B build -A x64 -DBAZZALT_BUILD_EDITOR_BRIDGE=ON
./scripts/build_windows_release.ps1 -Product core
./scripts/build_windows_release.ps1 -Product editor
./scripts/build_windows_release.ps1 -Product hub
```

Outputs are in `dist/releases/`. Each ZIP/installer has an adjacent JSON
entry with product, version, platform, architecture, URL, byte size, and SHA-256.
Use `-Version`, `-BuildDirectory`, `-OutputDirectory`, and
`-DownloadBaseUrl` for custom releases. Staging directories must be clean;
the packager refuses to merge stale payloads. `build_production.ps1` is a
compatibility wrapper (`-Product all` builds all three separately).
`build_installer.ps1` now accepts only a Hub-only bundle and validates it.

Every shipped EXE/DLL/PYD has its actual PE machine checked; changing a filename
or passing `-Architecture x86` cannot disguise x64 output. Python's pointer
size must match. Editor packaging runs the compiled executable's
`--check-runtime` smoke test before archiving.

## GitHub workflows and downloads

Independent workflows: Windows Editor, Windows Core, Windows Hub. Each can be
started manually, optionally overriding the product version. Their x64 jobs
build/test/package the product and upload its binaries and catalog entry.
Their separate x86 jobs upload explicitly named **support reports**, not binaries.
They do not claim a completed Win32 port.

Windows Release Suite builds all products plus a separate verified LLVM ZIP.
Manual dispatch creates CI artifacts only. A `release-v*` tag additionally
publishes a GitHub Release with the product files and `downloads.json`.
Change versions.json before creating a new release tag. Publishing requires the
repository Actions token to have contents-write permission. No workflow has been
executed or release uploaded merely by editing this repository.

Hub's Editors page accepts an HTTPS catalog URL and provides Check Downloads,
a package selector, Download and Install, progress, and Cancel. Download/install
work runs off the UI thread. SHA-256 and byte size are checked before bounded ZIP
extraction. Traversal, drive paths, ADS, Windows device names, symlinks, duplicate
case-insensitive paths, incomplete editors, and wrong-architecture PE files are
rejected. Installation is published by same-filesystem rename only after
validation; existing installations are never overwritten. Closing during a
download requests cancellation instead of destroying a running QThread.

Use a trusted catalog: it authorizes downloading executable code. Checksums protect
integrity, not an untrusted publisher. The default catalog points to this
repository's latest public Release. Before that release exists, the Hub reports
the HTTP failure; it does not invent available downloads.
[GitHub Actions downloads](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts)
require access and expire, so the public Hub uses stable Release asset URLs.
`scripts/make_download_catalog.py` can merge independently generated entries
with `--base-url` if publishing manually.

Windows C++ Build Tools is offered as a separate official Microsoft download.
The user must install the Desktop development with C++ workload and Windows SDK
through Microsoft's installer; Hub does not silently accept licenses or elevate.
LLVM is installed by Hub into shared Tools storage. Its release downloader
requires GitHub's official asset SHA-256 digest or an explicitly verified checksum.
Editor Preferences > Build Tools accepts an explicit Clang++ executable and SDK
directory. Invalid overrides do not fall back silently. Missing tools, SDKs, or
compiler launch failures become Console diagnostics and stop Play safely.

## Win32 investigation and remaining work

Qt 6's [supported Windows platforms](https://doc.qt.io/qt-6.10/supported-platforms.html)
are x64/ARM64, not x86; the pinned PySide6 release supplies no win32 wheel.
`scripts/check_x86_support.py` probes the actual win32 wheel and records the
pip output and installed Filament SDK availability. The official Filament
v1.77.0 Windows SDK contains x86_64 libraries only. CMake now selects x86 for a
real 32-bit target and fails clearly when matching libraries are absent.

A genuine port needs source-built Qt, Shiboken/PySide6, and Filament plus
transitive dependencies, with 32-bit Python. Filament's distribution directory
defaults to host architecture; a source Win32 experiment must set `DIST_ARCH=x86`
rather than install mislabeled x64 libraries. See its
[pinned build configuration](https://github.com/google/filament/blob/v1.77.0/CMakeLists.txt)
and [building guide](https://github.com/google/filament/blob/v1.77.0/BUILDING.md).
There is no general pointer-size prohibition in the inspected SDK headers,
but that is **not** evidence that the full Win32 renderer or GUI builds/runs.

After producing and testing matching custom dependencies, configure a separate
Win32 CMake tree with `-A Win32 -DBAZZALT_FILAMENT_DIR=<custom-sdk>` and run the
same packager with 32-bit Python and `-Architecture x86`. A 32-bit host also
requires compatible import/compiler executables. Shipping x86 remains blocked
until native, GUI, renderer, and installer tests pass; TODO 3 remains partial.

## Compiled resources and release hygiene

`scripts/compile_resources.py` generates editor, Hub, and shared branding Qt
resource modules. Nuitka compiles their byte arrays; raw application SVGs, locales,
layouts, and GLBs are not copied into installations. Development remains file-based.
The original docking resource package is unchanged. Audit commands reject raw
application resources. Embedding is packaging, not encryption; never embed secrets.

Inno Setup installs only the standalone Hub distribution. Updates no longer delete
or replace editor-version directories. Uninstall preserves projects and per-user
data. Code signing and GitHub release execution require the owner's credentials;
no signing is faked. Dependency licensing/notices should be audited before a
public production release.
