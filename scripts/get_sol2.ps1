$ErrorActionPreference = 'Stop'
$Destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'deps/sol2'
$Commit = 'dca62a0f02bb45f3de296de3ce00b1275eb34c25'
if (-not (Test-Path "$Destination/.git")) {
    if (Test-Path $Destination) { throw 'Sol2 destination already exists' }
    git clone --branch v3.3.1 --depth 1 https://github.com/ThePhD/sol2.git $Destination
    if ($LASTEXITCODE -ne 0) { throw 'Sol2 download failed' }
}
$Actual = (git -C $Destination rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Actual -ne $Commit) { throw 'Sol2 source pin mismatch' }
