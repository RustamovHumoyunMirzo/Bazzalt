param(
    [Parameter(Mandatory=$true)][ValidateSet('editor','core','hub')][string]$Product,
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [string]$Version = '',
    [string]$BuildDirectory = 'build',
    [string]$OutputDirectory = 'dist/releases',
    [string]$FilamentDirectory = 'deps/filament',
    [string]$DownloadBaseUrl = ''
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Versions = Get-Content (Join-Path $Root 'releases/versions.json') -Raw | ConvertFrom-Json
if (-not $Version) { $Version = $Versions.$Product }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be major.minor.patch' }
$Build = [IO.Path]::GetFullPath((Join-Path $Root $BuildDirectory))
$Output = [IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
$Filament = [IO.Path]::GetFullPath((Join-Path $Root $FilamentDirectory))
$env:NUITKA_CACHE_DIR = Join-Path $Build 'NuitkaCache'
$PythonBits = (python -c 'import struct; print(struct.calcsize("P") * 8)').Trim()
if ($LASTEXITCODE -ne 0 -or $PythonBits -ne $(if ($Architecture -eq 'x64') {'64'} else {'32'})) {
    throw "Python architecture must match $Architecture; x86 requires a real custom Qt/PySide6 build."
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Stage = Join-Path $Output "$Product-$Version-windows-$Architecture"
if (Test-Path -LiteralPath $Stage) { throw "Use a clean output directory; refusing to merge stale payload: $Stage" }
function Copy-ReleaseSDK([string]$Destination) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'lib') | Out-Null
    Copy-Item -LiteralPath (Join-Path $Build 'ScriptSDK/include') -Destination $Destination -Recurse
    Copy-Item -LiteralPath (Join-Path $Build 'Editor/Release/Bazzalt.dll') -Destination (Join-Path $Destination 'lib/Bazzalt.dll')
    Copy-Item -LiteralPath (Join-Path $Build 'ScriptSDK/lib/Release/Bazzalt.lib') -Destination (Join-Path $Destination 'lib/Bazzalt.lib')
}
if ($Product -ne 'hub') {
    $Target = if ($Product -eq 'editor') {'_bazzalt_runtime'} else {'Bazzalt'}
    cmake --build $Build --config Release --target $Target
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
}
if ($Product -eq 'core') {
    New-Item -ItemType Directory -Path $Stage | Out-Null
    Copy-ReleaseSDK $Stage
    Copy-Item -LiteralPath (Join-Path $Root 'LICENSE') -Destination $Stage
    @{version=$Version;architecture=$Architecture;platform='windows';abi=1} | ConvertTo-Json | Set-Content (Join-Path $Stage 'core.json') -Encoding utf8
} else {
    python (Join-Path $Root 'scripts/compile_resources.py') --build-directory (Join-Path $Build 'CompiledResources')
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
    $Entry = if ($Product -eq 'hub') {'bazzalt_hub'} else {'bazzalt_editor'}
    $Executable = if ($Product -eq 'hub') {'BazzaltHub.exe'} else {'Bazzalt.exe'}
    $Resource = if ($Product -eq 'hub') {'hub'} else {'editor'}
    $NuitkaOutput = Join-Path $Output "nuitka-$Product-$Architecture"
    if (Test-Path -LiteralPath $NuitkaOutput) { throw 'Nuitka output must be clean' }
    python -m nuitka --mode=standalone --msvc=latest --assume-yes-for-downloads --enable-plugin=pyside6 `
        --nofollow-import-to=PySide6.QtWebEngineCore,PySide6.QtWebEngineWidgets,PySide6.QtWebChannel `
        "--include-module=bazzalt._${Resource}_resources_rc" --include-module=bazzalt._branding_resources_rc `
        --windows-console-mode=disable "--output-filename=$Executable" "--output-dir=$NuitkaOutput" (Join-Path $Root "$Entry.py")
    if ($LASTEXITCODE -ne 0) { throw 'Nuitka compilation failed' }
    Move-Item -LiteralPath (Join-Path $NuitkaOutput "$Entry.dist") -Destination $Stage
    if ($Product -eq 'editor') {
        $NativeFolder = Join-Path $Build 'Editor/Release'
        $Native = Get-ChildItem -LiteralPath $NativeFolder -Filter '_bazzalt_runtime*.pyd' | Select-Object -First 1
        if (-not $Native) { throw 'Native editor module missing' }
        New-Item -ItemType Directory -Force -Path (Join-Path $Stage 'Editor') | Out-Null
        foreach ($File in @($Native.FullName, (Join-Path $NativeFolder 'Bazzalt.dll'), (Join-Path $NativeFolder 'bshad.dll'))) {
            Copy-Item -LiteralPath $File -Destination (Join-Path $Stage 'Editor')
        }
        Copy-ReleaseSDK (Join-Path $Stage 'ScriptSDK')
        New-Item -ItemType Directory -Path (Join-Path $Stage 'tools/filament') -Force | Out-Null
        foreach ($Tool in @('matc.exe','cmgen.exe','filamesh.exe')) {
            Copy-Item -LiteralPath (Join-Path $Filament "bin/$Tool") -Destination (Join-Path $Stage 'tools/filament')
        }
        @{version=$Version;core_version=$Versions.core;architecture=$Architecture;executable='Bazzalt.exe';project_format_max=1} | ConvertTo-Json | Set-Content (Join-Path $Stage 'editor.json') -Encoding utf8
        $Check = Start-Process -FilePath (Join-Path $Stage 'Bazzalt.exe') -ArgumentList '--check-runtime' -WorkingDirectory $Stage -WindowStyle Hidden -Wait -PassThru
        if ($Check.ExitCode -ne 0) { throw 'Packaged native runtime check failed' }
    }
    python (Join-Path $Root 'scripts/compile_resources.py') --audit $Stage
    if ($LASTEXITCODE -ne 0) { throw 'Compiled resource audit failed' }
}
if ($Product -eq 'hub') {
    python -c "import sys; sys.path.insert(0, r'$PSScriptRoot'); from release_artifacts import Audit; from pathlib import Path; Audit(Path(r'$Stage'), '$Architecture', 'hub')"
    if ($LASTEXITCODE -ne 0) { throw 'Hub payload audit failed' }
    & (Join-Path $PSScriptRoot 'build_installer.ps1') -Version $Version -Architecture $Architecture -BundleDirectory $Stage -OutputDirectory $Output
    $Artifact = Join-Path $Output "BazzaltHub-$Version-windows-$Architecture-Setup.exe"
    python (Join-Path $PSScriptRoot 'release_artifacts.py') --product hub --architecture $Architecture --version $Version --output $Artifact --base-url $DownloadBaseUrl
} else {
    $Artifact = Join-Path $Output "Bazzalt-$Product-$Version-windows-$Architecture.zip"
    python (Join-Path $PSScriptRoot 'release_artifacts.py') --root $Stage --product $Product --architecture $Architecture --version $Version --output $Artifact --base-url $DownloadBaseUrl
}
if ($LASTEXITCODE -ne 0) { throw 'Artifact validation failed' }
Write-Host "Release artifact: $Artifact"
