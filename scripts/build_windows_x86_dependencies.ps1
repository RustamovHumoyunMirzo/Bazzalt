param(
    [ValidateSet('filament')][string]$Stage = 'filament',
    [string]$WorkDirectory = 'build/win32-bootstrap',
    [ValidateRange(1,32)][int]$Parallel = 2,
    [switch]$AllowLocalBuild
)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -and -not $AllowLocalBuild) {
    throw 'Large dependency builds belong in GitHub Actions. Local builds require explicit -AllowLocalBuild opt-in.'
}
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Work = [IO.Path]::GetFullPath((Join-Path $Root $WorkDirectory))
if (-not $Work.StartsWith($Root + [IO.Path]::DirectorySeparatorChar)) { throw 'Win32 build work must stay inside the workspace.' }
$Versions = Get-Content (Join-Path $Root 'releases/versions.json') -Raw | ConvertFrom-Json
$Pins = Get-Content (Join-Path $Root 'releases/windows-x86.json') -Raw | ConvertFrom-Json
if ($Versions.python -ne '3.13.2') { throw 'Update and verify the Win32 dependency pins when changing Python versions.' }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
. (Join-Path $PSScriptRoot 'windows_toolchain.ps1')
$HostPython = (Get-Command python).Source
function Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Checkout([string]$Repository, [string]$Tag, [string]$Commit, [string]$Destination) {
    if (-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
        Checked git @('clone','--depth','1','--branch',$Tag,$Repository,$Destination)
    }
    $Actual = (& git -C $Destination rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $Actual -ne $Commit) { throw "Dependency checkout failed integrity check: $Destination" }
}
function Build([string]$Source, [string]$Directory, [string]$Prefix, [string[]]$Options) {
    # Installed SDKs travel between CI jobs; build trees do not. Reuse only a
    # completed installation for the exact source, flags and compiler target.
    $Commit=(& git -C $Source rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Cannot identify dependency source: $Source" }
    $Identity=@($Commit,(Get-Command cl).Source)+$Options
    # Source compatibility patches are part of the installed SDK's identity.
    $SourceDiff=(& git -C $Source diff --no-ext-diff --binary) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "Cannot identify dependency patches: $Source" }
    $Identity+=([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($SourceDiff))))
    $Stamp=Join-Path $Prefix ("bazzalt-build-"+[IO.Path]::GetFileName($Directory)+'.json')
    $Expected=ConvertTo-Json -InputObject $Identity -Compress
    if ((Test-Path -LiteralPath $Stamp) -and ([IO.File]::ReadAllText($Stamp) -eq $Expected)) {
        Write-Host "Reusing verified completed installation: $Prefix"
        return
    }
    Checked cmake (@('-S',$Source,'-B',$Directory,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_INSTALL_PREFIX=$Prefix")+$Options)
    Checked cmake @('--build',$Directory,'--parallel',"$Parallel")
    Checked cmake @('--install',$Directory)
    [IO.File]::WriteAllText($Stamp,$Expected)
}
function Build-Filament {
    $Filament = Join-Path $Work 'filament-source'
    Checkout 'https://github.com/google/filament.git' 'v1.77.0' $Pins.filament_commit $Filament
    Checked $HostPython @((Join-Path $PSScriptRoot 'patch_filament_win32.py'),$Filament)
    Import-BazzaltMsvc x86
    Build $Filament (Join-Path $Work 'filament-x86-build') (Join-Path $Work 'filament-x86') @('-DDIST_ARCH=x86','-DDIST_DIR=x86/md','-DUSE_STATIC_CRT=OFF','-DFILAMENT_BUILD_FILAMAT=ON','-DFILAMENT_BUILD_TESTING=OFF','-DSPIRV_SKIP_TESTS=ON','-DFILAMENT_SUPPORTS_VULKAN=ON','-DFILAMENT_SUPPORTS_WEBGPU=OFF')
}
Checked $HostPython @('-c','import struct; assert struct.calcsize("P")==8, "Win32 bootstrap requires a separate x64 host Python"')
$Package = Join-Path $Work 'pythonx86.3.13.2.nupkg'
if (-not (Test-Path -LiteralPath $Package)) {
    Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/pythonx86/3.13.2/pythonx86.3.13.2.nupkg' -OutFile $Package
}
if ((Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash -ne $Pins.python_package_sha256) { throw 'Python x86 archive integrity check failed.' }
$PythonPackage = Join-Path $Work 'python-package'
if (-not (Test-Path -LiteralPath $PythonPackage)) { [IO.Compression.ZipFile]::ExtractToDirectory($Package,$PythonPackage) }
$Python = Join-Path $PythonPackage 'tools/python.exe'
Checked $Python @('-c','import struct; assert struct.calcsize("P")==4')
Build-Filament
