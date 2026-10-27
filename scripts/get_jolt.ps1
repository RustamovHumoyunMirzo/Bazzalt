$ErrorActionPreference='Stop'
python (Join-Path $PSScriptRoot 'get_jolt.py') @args
if ($LASTEXITCODE -ne 0) { throw 'Jolt source preparation failed.' }
