$ErrorActionPreference='Stop'
python "$PSScriptRoot/get_recast.py" @args
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
