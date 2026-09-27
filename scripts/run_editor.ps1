param(
    [string]$Project = "",
    [switch]$RebuildNative,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"
$Runtime = Get-ChildItem (Join-Path $Build "Editor/Release") -Filter "_bazzalt_runtime*.pyd" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $Project) { $Project = Join-Path $Root "Editor/tests/fixtures/Alpha.bproject" }
$Project = [System.IO.Path]::GetFullPath($Project)
if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) { throw "Project file was not found: $Project" }

$NativeSources = Get-ChildItem (Join-Path $Root "src"),(Join-Path $Root "include") -Recurse -File |
    Where-Object { $_.Extension -in ".cpp", ".h", ".hpp" }
$NewestSource = $NativeSources | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
$NativeStale = -not $Runtime -or ($NewestSource -and $NewestSource.LastWriteTimeUtc -gt $Runtime.LastWriteTimeUtc)

if (-not $NoBuild -and ($RebuildNative -or $NativeStale)) {
    if (-not (Test-Path -LiteralPath (Join-Path $Build "CMakeCache.txt"))) {
        cmake -S $Root -B $Build -DBAZZALT_BUILD_EDITOR_BRIDGE=ON
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    }
    cmake --build $Build --config Release --target _bazzalt_runtime
    if ($LASTEXITCODE -ne 0) { throw "Native editor runtime build failed" }
}

$env:PYTHONPATH = $Root
& python (Join-Path $Root "bazzalt_editor.py") --project $Project --editor-version 1.0.0
exit $LASTEXITCODE
