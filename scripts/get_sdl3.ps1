$ErrorActionPreference = "Stop"
$ProjectDirectory = Split-Path -Parent $PSScriptRoot
$DependencyDirectory = Join-Path $ProjectDirectory "deps/SDL3"
$Version = "release-3.2.30"
$Commit = "f5e5f6588921eed3d7d048ce43d9eb1ff0da0ffc"
if (-not (Test-Path (Join-Path $DependencyDirectory "CMakeLists.txt"))) {
    if (Test-Path $DependencyDirectory) { throw "SDL3 directory exists but is incomplete; refusing to overwrite it." }
    git clone --branch $Version --depth 1 https://github.com/libsdl-org/SDL.git $DependencyDirectory
    if ($LASTEXITCODE -ne 0) { throw "SDL3 download failed." }
}
$ResolvedCommit = (git -C $DependencyDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedCommit -ne $Commit) { throw "SDL3 integrity check failed: expected $Commit, got $ResolvedCommit." }
Write-Host "SDL3 $Version is ready at $DependencyDirectory"
