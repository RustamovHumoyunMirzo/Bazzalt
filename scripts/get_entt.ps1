$ErrorActionPreference = "Stop"

$ProjectDirectory = Split-Path -Parent $PSScriptRoot
$EnttDirectory = Join-Path $ProjectDirectory "deps/entt"
$EnttVersion = "v3.13.2"

if (Test-Path (Join-Path $EnttDirectory "CMakeLists.txt")) {
    Write-Host "EnTT is already available at $EnttDirectory"
    exit 0
}

if (Test-Path $EnttDirectory) {
    if ((Get-ChildItem -LiteralPath $EnttDirectory -Force).Count -ne 0) {
        throw "$EnttDirectory exists and is not an empty directory."
    }
    Remove-Item -LiteralPath $EnttDirectory
}

git clone --branch $EnttVersion --depth 1 `
    https://github.com/skypjack/entt.git $EnttDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Failed to download EnTT. Ensure Git is installed and try again."
}

Write-Host "Installed EnTT $EnttVersion at $EnttDirectory"
