$ErrorActionPreference = "Stop"

$ProjectDirectory = Split-Path -Parent $PSScriptRoot
$FilamentDirectory = Join-Path $ProjectDirectory "deps/filament"
$ArchivePath = Join-Path $ProjectDirectory "deps/filament-v1.77.0-windows.tgz"
$DownloadUrl = "https://github.com/google/filament/releases/download/v1.77.0/filament-v1.77.0-windows.tgz"

if (Test-Path (Join-Path $FilamentDirectory "include/filament/Engine.h")) {
    Write-Host "Filament is already available at $FilamentDirectory"
    exit 0
}
if (Test-Path $FilamentDirectory) {
    if ((Get-ChildItem -LiteralPath $FilamentDirectory -Force).Count -ne 0) {
        throw "$FilamentDirectory exists and is not an empty directory."
    }
} else {
    New-Item -ItemType Directory -Path $FilamentDirectory | Out-Null
}

try {
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ArchivePath
    tar -xzf $ArchivePath -C $FilamentDirectory
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
} finally {
    if (Test-Path $ArchivePath) { Remove-Item -LiteralPath $ArchivePath }
}

if (-not (Test-Path (Join-Path $FilamentDirectory "include/filament/Engine.h"))) {
    throw "Downloaded Filament package has an unexpected layout."
}
Write-Host "Installed Filament v1.77.0 at $FilamentDirectory"
