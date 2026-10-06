$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Destination = Join-Path $Root 'deps/lua'
$Version = '5.4.9'
$Hash = '2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6'
if (Test-Path "$Destination/src/lua.h") {
    if ((Get-Content "$Destination/.bazzalt-source-sha256" -Raw).Trim() -ne $Hash) { throw 'Lua source pin mismatch' }
    exit 0
}
if (Test-Path $Destination) { throw 'Lua destination already exists; refusing to overwrite it' }
$Archive = Join-Path $Root 'deps/lua-source.tar.gz'
New-Item -ItemType Directory -Force (Join-Path $Root 'deps') | Out-Null
Invoke-WebRequest "https://www.lua.org/ftp/lua-$Version.tar.gz" -OutFile $Archive
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) { throw 'Lua archive integrity check failed' }
& tar -xzf $Archive -C (Join-Path $Root 'deps')
if ($LASTEXITCODE -ne 0) { throw 'Lua extraction failed' }
Move-Item -LiteralPath (Join-Path $Root "deps/lua-$Version") -Destination $Destination
Set-Content "$Destination/.bazzalt-source-sha256" $Hash -Encoding ascii
