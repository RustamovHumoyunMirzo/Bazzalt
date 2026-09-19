$ErrorActionPreference = "Stop"

$ProjectDirectory = Split-Path -Parent $PSScriptRoot
$RymlDirectory = Join-Path $ProjectDirectory "deps/rapidyaml"
$RymlVersion = "v0.16.0"

if (Test-Path (Join-Path $RymlDirectory "CMakeLists.txt")) {
    Write-Host "rapidyaml is already available at $RymlDirectory"
    exit 0
}
if (Test-Path $RymlDirectory) {
    if ((Get-ChildItem -LiteralPath $RymlDirectory -Force).Count -ne 0) {
        throw "$RymlDirectory exists and is not an empty directory."
    }
    Remove-Item -LiteralPath $RymlDirectory
}
git clone --branch $RymlVersion --depth 1 --recurse-submodules --shallow-submodules `
    https://github.com/biojppm/rapidyaml.git $RymlDirectory
if ($LASTEXITCODE -ne 0) { throw "Failed to download rapidyaml." }
Write-Host "Installed rapidyaml $RymlVersion at $RymlDirectory"
