$ErrorActionPreference = 'Stop'
python (Join-Path $PSScriptRoot 'get_lua.py') @args
if ($LASTEXITCODE -ne 0) { throw 'Lua source preparation failed.' }
