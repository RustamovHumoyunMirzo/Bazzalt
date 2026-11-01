$ErrorActionPreference='Stop'
python (Join-Path $PSScriptRoot 'get_yojimbo.py')
if ($LASTEXITCODE -ne 0) { throw 'Yojimbo source fetch failed' }
