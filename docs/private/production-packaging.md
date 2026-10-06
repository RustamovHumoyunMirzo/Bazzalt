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

Windows 32-bit and Windows 64-bit run on every push and pull request, and can
also be started manually. Each calls the shared product pipeline for only its
architecture: x64 builds the editor, while x86 builds only the game runtime,
checking the packaged runtime. README badges show GitHub's actual push-workflow
status, not a hard-coded passing label. Superseded runs on the same ref are
cancelled to avoid unnecessary dependency builds. These checks run entirely on
Actions workers, not the developer's machine.

Windowless native rendering/resource tests explicitly use Filament's NOOP driver
instead of requiring a GPU on hosted workers. This exercises real resource
managers, materials, and teardown, but is not a pixel-rendering test. Normal editor
and game initialization still uses the selected native graphics backend. Test-only
diagnostics route Windows assertions to stderr and print unhandled exceptions;
they do not suppress assertions or turn failures into successful tests. Both
architecture jobs preserve verbose CTest logs and JUnit reports in separate
`test-logs-windows-*` artifacts even when testing fails. These diagnostics are
excluded from the Release Suite's product-artifact download pattern.

Independent workflows: Windows Editor, Windows Core, Windows Hub. Each can be
started manually, optionally overriding the product version. Their x64 jobs
build/test/package the product and upload its binaries and catalog entry.
Only Core additionally builds a genuine Win32 SDK. Editor and Hub are x64-only.

Windows Release Suite builds x64 Editor/Hub/LLVM and x64/x86 Core packages.
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

## Win32 game-runtime targets

Hub and Editor require a 64-bit machine and process. Win32 support is limited
 to the core/game-runtime SDK used for producing 32-bit games from a 64-bit
 development environment. No Win32 Qt, PySide, Hub, Editor or compiler-host
 packages are built. The 64-bit compiler targets x86 when producing games.

The Windows 32-bit Game Runtime workflow builds and tests only core. It builds
 pinned Filament using the Win32 MSVC toolchain and compatibility patches,
 caches the installed SDK, and preserves native test logs. The editor bridge
 must be disabled with BAZZALT_BUILD_EDITOR_BRIDGE=OFF for 32-bit builds.
 Release Suite ships x64 Hub/Editor/tools and both core architectures.

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
