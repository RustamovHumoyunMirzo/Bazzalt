# Contributor verification

## Fast feedback

Use an existing matching SDK/build directory. Routine work should not rebuild
Filament, Qt or other large dependencies. Production dependency builds belong in
platform workflows. Separate directories for compilers, architectures and provider sets.

For editor-only changes, run source mode through `scripts/run_editor.ps1` or
`scripts/run_editor.sh`. Rebuild affected native targets for C++ changes. Close
the editor before replacing its loaded libraries.

| Changed contract | Useful verification |
| --- | --- |
| Provider/build/loading | Dependency-free provider tests, loader test, affected native suites |
| Component/data | Current/old round-trip, invalid data, reference remapping |
| Lua/native scripts | Real VM/module execution, stop/reload and callback cleanup |
| Editor | Unit tests plus interaction, theme/size/selection/history |
| Renderer | Render/readback or actual viewport check, resize and teardown |
| Import/cooking | Bad input, cache invalidation, dependency graph, cooked loading |
| Releases | Staging, platform/architecture audit, standalone output |
| Public APIs | Both bindings, generated reference, examples and What's New |
| Contributor docs | Local links and handbook index coverage |

## Commands

```powershell
python -m unittest discover -s tests -p test_runtime_modules.py -v
cmake --build build --config Release --target BazzaltRuntime
cmake --build build --config Release --target BazzaltModuleLoaderTests
ctest --test-dir build -C Release -R BazzaltModuleLoaderTests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Provider tests configure/build a tiny fixture without engine dependencies. The
native loader test uses the actual sibling AI module, including missing entries,
wrong version/size and changed working directory.

For Windows Filament fetching changes, run `python -m unittest discover -s tests
-p test_filament_download.py`. It simulates transient/persistent network failures
and verifies that checksum failures prevent extraction, without downloading the SDK.

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
$env:PYTHONUTF8 = '1'
python -m unittest discover -s Editor/tests -t .
python -m unittest discover -s Compiler/tests
python -m unittest discover -s Launcher/tests
```

Run native linking and tests importing native Python sequentially on Windows.
Python processes can hold DLLs after windows disappear. Offscreen Qt tests still
need interactive checks for native surfaces, capture and window placement.

```powershell
python scripts/check_contributor_docs.py
python scripts/sync_api_reference.py --check
python scripts/build_public_docs.py --check
python -m unittest discover -s tests -p test_api_reference.py
python -m unittest discover -s tests -p 'test_public_docs*.py'
git diff --check
```

Refresh stale output using the same generator without `--check`, review it, then
recheck. Edit narrative HTML outside `API-SOURCE` markers. See
[reference maintenance](api-reference-maintenance.md).

## Platform confidence and pull requests

Windows x64 is the local editor development path on this machine. Windows/Linux
x86 support game output. Editor/Hub require 64-bit. Exercise Linux, macOS and
Android paths in matching workflows/devices. Portable tests must handle Unicode
paths and run without assuming a CWD, installed editor or GPU in headless CI.

Keep strict checks when diagnosing failures. Reuse numeric, path and loader
helpers where their contracts already exist. Report actual checks, failures and
unavailable platform/device coverage. Update the owning guide when its maintenance
path changes. Parsing/configuration tests alone cannot establish rendered output.
