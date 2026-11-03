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
$Downloaded = $false
foreach ($HostUrl in @('https://www.lua.org/ftp', 'https://www.tecgraf.puc-rio.br/lua/ftp', 'https://lua.org/ftp')) {
    for ($Attempt = 1; $Attempt -le 2; ++$Attempt) {
        try {
            Invoke-WebRequest "$HostUrl/lua-$Version.tar.gz" -OutFile $Archive -TimeoutSec 90
            $Downloaded = $true
            break
        } catch {
            Write-Warning "Lua download failed from $HostUrl (attempt $Attempt/2)."
        }
    }
    if ($Downloaded) { break }
}
if (-not $Downloaded) { throw 'All pinned Lua download endpoints failed' }
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) { throw 'Lua archive integrity check failed' }
& tar -xzf $Archive -C (Join-Path $Root 'deps')
if ($LASTEXITCODE -ne 0) { throw 'Lua extraction failed' }
Move-Item -LiteralPath (Join-Path $Root "deps/lua-$Version") -Destination $Destination
Set-Content "$Destination/.bazzalt-source-sha256" $Hash -Encoding ascii
