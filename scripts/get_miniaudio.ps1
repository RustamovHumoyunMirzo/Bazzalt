$ErrorActionPreference = 'Stop'
$DependencyDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'deps/miniaudio'
$Commit = 'f40cf03f80cdb7e741d43e53b7e706e8c1394bcf'
if (-not (Test-Path (Join-Path $DependencyDirectory 'miniaudio.h'))) {
    if (Test-Path $DependencyDirectory) { throw 'miniaudio directory is incomplete; refusing to overwrite it.' }
    git clone --branch 0.11.23 --depth 1 https://github.com/mackron/miniaudio.git $DependencyDirectory
    if ($LASTEXITCODE -ne 0) { throw 'miniaudio download failed.' }
}
if ((git -C $DependencyDirectory rev-parse HEAD).Trim() -ne $Commit) { throw 'miniaudio integrity check failed.' }
Write-Host "miniaudio 0.11.23 is ready at $DependencyDirectory"
