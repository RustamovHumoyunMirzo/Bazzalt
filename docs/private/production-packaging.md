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
architecture, building the engine/editor, running native and Python tests, and
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
Their x86 jobs now source-build matching dependencies and attempt the same
test/package pipeline with genuine Win32 targets. They upload binaries only after
all build, test, and architecture checks pass; support reports are no longer
substituted for products. The complete Win32 pipeline still needs a successful
GitHub Actions run before it is considered release-validated.

Windows Release Suite builds both architectures of all products plus separate
verified x64 and source-built Win32 LLVM ZIPs.
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

## Source-built Win32 pipeline

Qt 6's [supported Windows platforms](https://doc.qt.io/qt-6.10/supported-platforms.html)
are x64/ARM64, not x86; the pinned PySide6 release supplies no win32 wheel.
The official Filament v1.77.0 Windows SDK also contains x86_64 libraries only.
The Win32 jobs therefore cannot use those precompiled packages. CMake selects
`x86` for a real 32-bit target and fails when matching libraries are absent.

`releases/windows-x86.json` pins the QtBase, QtSvg, PySide/Shiboken, Filament,
and LLVM source commits, plus the checksum of portable Python 3.13.2 Win32.
`scripts/build_windows_x86_dependencies.ps1` runs on the Actions worker:

- `qt` builds the installed Win32 Qt base and QtSvg SDK.
- `gui` reuses the completed Qt/QtSvg installation, builds a separate minimal x64 Qt/Shiboken generator,
  and genuine Win32 PySide bindings; it builds architecture-audited wheels and
  performs a Qt Widgets/SVG smoke test with 32-bit Python.
- `filament` builds only the native renderer dependencies needed by Core.
  Core's SVG build-time generator uses host Python/Qt, not shipped GUI libraries.
- `all` prepares the Editor's GUI and native renderer dependencies.

Filament is built with the x86 MSVC toolchain, the dynamic CRT, Vulkan enabled,
and `DIST_DIR=x86/md`. Its import tools are also compiled as Win32 executables.
Host generator tools never enter the shipped payload.
Both Qt source builds explicitly disable the unused QtSql module; this prevents
auto-detected database clients installed on hosted runners from introducing
wrong-architecture PostgreSQL/MySQL plugins into the Win32 build.
The bindings wheel helper
checks every DLL/PYD/EXE and creates ordinary metadata and hashed RECORD entries;
it does not supply replacement or stub Qt APIs.

`scripts/build_windows_x86_llvm.ps1` builds actual Win32 Clang and LLD with an
`i686-pc-windows-msvc` target and resource headers, then audits and packages them
as a separate tools download. The Win32 compiler is not bundled into Editor.

Product jobs configure a separate CMake tree with `-A Win32`, run native tests,
run the GUI regression suite where applicable, and invoke the common packager
with 32-bit Python, `-Architecture x86`, and `-FilamentDirectory` pointing to the
source-built SDK. Editor packaging additionally checks the packaged native runtime.
The Hub job creates an independent x86 Inno Setup installer containing only Hub.
Release publication waits for **both architectures** of every product and tool
package. A failed upstream port/build never produces a mislabeled x86 release.

These are large **CI builds**, not automatic local developer setup. Both source
build scripts refuse execution outside GitHub Actions unless a developer explicitly
opts in with `-AllowLocalBuild`. Trigger Windows Editor/Core/Hub or Windows Release
Suite in Actions to validate the setup. Only source, pins, scripts, and workflows
belong in the repository; dependency build/cache directories remain ignored.
Local dependency compilation was stopped at the owner's request. Successful
partial compilation is not proof of a working Win32 Editor/renderer/installer;
TODO 3 remains pending end-to-end CI validation.

### Win32 job duration and caching

The reusable product workflow no longer puts every source build and packaging
step into a single 350-minute job. It uses four stages:

1. `x86-qt`: Qt base and SVG, up to 350 minutes.
2. `x86-gui`: depends on Qt; builds generators and PySide, up to 350 minutes.
3. `x86-filament`: independent of Qt/PySide, up to 350 minutes.
4. `x86`: downloads the completed runtimes/SDKs, tests and packages the product,
   up to 120 minutes. Core skips GUI stages; Hub skips Filament.

Each dependency stage caches only its completed installation, keyed by pinned
sources, build/packaging scripts and MSVC toolset version. There are no fuzzy
restore keys. Exact completed-installation stamps stop the GUI stage from
recompiling Qt after its SDK is downloaded. Fresh products still build and test
on every commit; cached dependencies do not skip product tests.

Intermediate artifacts have one-day retention and product-qualified names to
avoid collisions in release workflows. Cache misses still perform real source
builds; cache availability is an optimization, not a requirement. If one
individual dependency stage itself reaches the time limit, split that stage
further or use a more capable runner. This job structure has been statically
validated locally, but timing must be verified by an actual Actions run.

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
