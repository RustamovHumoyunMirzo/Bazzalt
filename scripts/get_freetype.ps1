$ErrorActionPreference='Stop'
$DependencyDirectory=Join-Path (Split-Path -Parent $PSScriptRoot) 'deps/freetype'
$Commit='0a0221a1347e2f1e07c395263540026e9a0aa7c7'
if (-not (Test-Path (Join-Path $DependencyDirectory 'CMakeLists.txt'))) {
    if (Test-Path $DependencyDirectory) { throw 'Incomplete FreeType directory; refusing to overwrite it.' }
    git clone --depth 1 --branch VER-2-14-3 https://github.com/freetype/freetype.git $DependencyDirectory
    if ($LASTEXITCODE -ne 0) { throw 'FreeType download failed.' }
}
if ((git -C $DependencyDirectory rev-parse HEAD).Trim() -ne $Commit) { throw 'FreeType integrity check failed.' }
Write-Host 'FreeType 2.14.3 source is ready.'
