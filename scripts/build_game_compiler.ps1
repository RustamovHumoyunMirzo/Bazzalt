param([string]$BuildDirectory='build',[string]$OutputDirectory='dist/compiler')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build=[IO.Path]::GetFullPath((Join-Path $Root $BuildDirectory))
$Output=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Output){throw 'Compiler output must be clean'}
python -m nuitka --mode=standalone --msvc=latest --assume-yes-for-downloads --nofollow-import-to=PySide6,Editor.gui,Editor.runtime,Compiler.tests --include-module=Editor.scripting --include-module=Editor.property_rules --include-module=Editor.lua_declarations --include-module=scripts.release_artifacts "--include-data-files=$Root/releases/versions.json=releases/versions.json" --output-filename=BazzaltCompiler.exe "--output-dir=$Output" (Join-Path $Root 'bazzalt_compiler.py')
if($LASTEXITCODE -ne 0){throw 'Standalone compiler build failed'}
$Stage=Join-Path $Output 'bazzalt_compiler.dist'
foreach($Name in @('BazzaltCooker.exe','Bazzalt.dll','bazzalt_lua.dll','bazzalt_gui.dll','bshad.dll')){Copy-Item -LiteralPath (Join-Path $Build "Editor/Release/$Name") -Destination $Stage}
Get-ChildItem -LiteralPath (Join-Path $Build 'ScriptSDK/lib') -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $Stage -Force}
New-Item -ItemType Directory -Path (Join-Path $Stage 'filament') | Out-Null
foreach($Tool in @('matc.exe','cmgen.exe','filamesh.exe')){Copy-Item -LiteralPath (Join-Path $Root "deps/filament/bin/$Tool") -Destination (Join-Path $Stage 'filament')}
Write-Host "Game compiler: $Stage"
