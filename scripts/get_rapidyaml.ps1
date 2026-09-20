$ErrorActionPreference = "Stop"

$ProjectDirectory = Split-Path -Parent $PSScriptRoot
$RymlDirectory = Join-Path $ProjectDirectory "deps/rapidyaml"
$RymlVersion = "v0.16.0"
$RymlCommit = "f8ac8dd50f4f7916579d55a05ebf9c6488e52670"

if (Test-Path (Join-Path $RymlDirectory "CMakeLists.txt")) {
    $ResolvedCommit = (git -C $RymlDirectory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ResolvedCommit -ne $RymlCommit) {
        throw "Existing rapidyaml checkout failed integrity check. Expected $RymlCommit, got $ResolvedCommit."
    }
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
$ResolvedCommit = (git -C $RymlDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedCommit -ne $RymlCommit) {
    throw "rapidyaml integrity check failed. Expected $RymlCommit, got $ResolvedCommit."
}
Write-Host "Installed rapidyaml $RymlVersion at $RymlDirectory"
