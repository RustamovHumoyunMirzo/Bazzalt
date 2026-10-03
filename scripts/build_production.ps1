param(
    [string]$Version = "1.0.0",
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist/production",
    [switch]$CreateInstaller
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Build = Join-Path $Root $BuildDirectory
$Output = Join-Path $Root $OutputDirectory
$VersionFolder = "bazzalt_" + $Version.Replace('.', '_')
$env:NUITKA_CACHE_DIR = Join-Path $Build "NuitkaCache"

python -c "import nuitka" 2>$null
if ($LASTEXITCODE -ne 0) { throw "Nuitka is missing. Run: python -m pip install -r requirements-build.txt" }

cmake --build $Build --config Release --target _bazzalt_runtime
if ($LASTEXITCODE -ne 0) { throw "The native BAZZALT editor runtime failed to build" }
$Native = Get-ChildItem -Path (Join-Path $Build "Editor/Release") -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^_bazzalt_runtime.*\.(pyd|so)$' } |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if (-not $Native) { throw "Could not find the built _bazzalt_runtime module" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 `
    --include-data-file="$Root/Launcher/index.html=Launcher/index.html" `
    --include-data-file="$Root/Launcher/BazzaltLogo.svg=Launcher/BazzaltLogo.svg" `
    --windows-console-mode=disable --output-filename=BazzaltHub.exe --output-dir=$Output "$Root/bazzalt_hub.py"
if ($LASTEXITCODE -ne 0) { throw "BazzaltHub compilation failed" }

python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 --windows-console-mode=disable `
    --include-data-dir="$Root/Editor/assets=Editor/assets" `
    --include-data-file="$Root/Launcher/BazzaltLogo.svg=Launcher/BazzaltLogo.svg" `
    --output-filename=Bazzalt.exe --output-dir=$Output "$Root/bazzalt_editor.py"
if ($LASTEXITCODE -ne 0) { throw "Bazzalt editor compilation failed" }

$Hub = Join-Path $Output "bazzalt_hub.dist"
$EditorBuild = Join-Path $Output "bazzalt_editor.dist"
$EditorTarget = Join-Path $Hub "versions/$VersionFolder"
New-Item -ItemType Directory -Force -Path $EditorTarget | Out-Null
Copy-Item -Path (Join-Path $EditorBuild "*") -Destination $EditorTarget -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $EditorTarget "Editor") | Out-Null
Copy-Item -LiteralPath $Native.FullName -Destination (Join-Path $EditorTarget "Editor/$($Native.Name)") -Force
$Bshader = Join-Path $Build "Editor/Release/bshad.dll"
if (-not (Test-Path -LiteralPath $Bshader)) { throw "Bshader editor translator is missing" }
Copy-Item -LiteralPath $Bshader -Destination (Join-Path $EditorTarget "Editor/bshad.dll") -Force
New-Item -ItemType Directory -Force -Path (Join-Path $EditorTarget "tools/filament") | Out-Null
Copy-Item -LiteralPath (Join-Path $Root "deps/filament/bin/matc.exe") -Destination (Join-Path $EditorTarget "tools/filament/matc.exe") -Force
Copy-Item -LiteralPath (Join-Path $Root "deps/filament/bin/cmgen.exe") -Destination (Join-Path $EditorTarget "tools/filament/cmgen.exe") -Force
$Toolchain = Join-Path $Root "toolchain/llvm"
if (-not (Test-Path (Join-Path $Toolchain "bin/clang++.exe"))) { throw "Bundled LLVM is missing. Run scripts/get_llvm.ps1 before production packaging." }
New-Item -ItemType Directory -Force -Path (Join-Path $EditorTarget "toolchain") | Out-Null
Copy-Item -LiteralPath $Toolchain -Destination (Join-Path $EditorTarget "toolchain/llvm") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $Root "include") -Destination (Join-Path $EditorTarget "include") -Recurse -Force
$Manifest = @{ version=$Version; executable="Bazzalt.exe"; project_format_max=1 } | ConvertTo-Json
$Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $EditorTarget "editor.json"), $Manifest, $Utf8WithoutBom)
$RuntimeCheck = Start-Process -FilePath (Join-Path $EditorTarget "Bazzalt.exe") `
    -ArgumentList "--check-runtime" -WorkingDirectory $EditorTarget -WindowStyle Hidden -Wait -PassThru
if ($RuntimeCheck.ExitCode -ne 0) { throw "Packaged editor could not load _bazzalt_runtime (exit $($RuntimeCheck.ExitCode))" }
Write-Host "Production bundle: $Hub"
if ($CreateInstaller) {
    & (Join-Path $PSScriptRoot "build_installer.ps1") -Version $Version `
        -BundleDirectory $Hub
}
