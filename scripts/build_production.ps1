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
$Native = Get-ChildItem -Path $Build -Recurse -File | Where-Object { $_.Name -match '^_bazzalt_runtime.*\.(pyd|so)$' } | Select-Object -First 1
if (-not $Native) { throw "Could not find the built _bazzalt_runtime module" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 `
    --include-data-file="$Root/Launcher/index.html=Launcher/index.html" `
    --windows-console-mode=disable --output-filename=BazzaltHub.exe --output-dir=$Output "$Root/bazzalt_hub.py"
if ($LASTEXITCODE -ne 0) { throw "BazzaltHub compilation failed" }

python -m nuitka --mode=standalone --assume-yes-for-downloads --enable-plugin=pyside6 --windows-console-mode=disable `
    --include-data-dir="$Root/Editor/assets=Editor/assets" `
    --output-filename=Bazzalt.exe --output-dir=$Output "$Root/bazzalt_editor.py"
if ($LASTEXITCODE -ne 0) { throw "Bazzalt editor compilation failed" }

$Hub = Join-Path $Output "bazzalt_hub.dist"
$EditorBuild = Join-Path $Output "bazzalt_editor.dist"
$EditorTarget = Join-Path $Hub "versions/$VersionFolder"
New-Item -ItemType Directory -Force -Path $EditorTarget | Out-Null
Copy-Item -Path (Join-Path $EditorBuild "*") -Destination $EditorTarget -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $EditorTarget "Editor") | Out-Null
Copy-Item -LiteralPath $Native.FullName -Destination (Join-Path $EditorTarget "Editor/$($Native.Name)") -Force
$Manifest = @{ version=$Version; executable="Bazzalt.exe"; project_format_max=1 } | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $EditorTarget "editor.json") -Value $Manifest -Encoding utf8
Write-Host "Production bundle: $Hub"
if ($CreateInstaller) {
    & (Join-Path $PSScriptRoot "build_installer.ps1") -Version $Version `
        -BundleDirectory ([System.IO.Path]::GetRelativePath($Root, $Hub))
}
