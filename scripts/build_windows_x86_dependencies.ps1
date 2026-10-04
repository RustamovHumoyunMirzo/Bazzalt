param(
    [ValidateSet('qt','gui','filament','all')][string]$Stage = 'all',
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
if ($Versions.python -ne '3.13.2' -or $Versions.pyside -ne $Pins.qt_version) { throw 'Update and verify the Win32 dependency pins when changing Python/Qt versions.' }
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
    Checked cmake (@('-S',$Source,'-B',$Directory,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_INSTALL_PREFIX=$Prefix")+$Options)
    Checked cmake @('--build',$Directory,'--parallel',"$Parallel")
    Checked cmake @('--install',$Directory)
}
function Build-Filament {
    $Filament = Join-Path $Work 'filament-source'
    Checkout 'https://github.com/google/filament.git' 'v1.77.0' $Pins.filament_commit $Filament
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
if ($Stage -eq 'filament') { Build-Filament; return }
$QtBase = Join-Path $Work 'qtbase'
Checkout 'https://github.com/qt/qtbase.git' "v$($Pins.qt_version)" $Pins.qtbase_commit $QtBase
Import-BazzaltMsvc x86
$Qt = Join-Path $Work 'qt-x86'
Build $QtBase (Join-Path $Work 'qtbase-x86') $Qt @('-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF','-DFEATURE_openssl=OFF','-DFEATURE_icu=OFF')
if ($Stage -eq 'qt') { Write-Host "Win32 Qt base: $Qt"; return }
$Svg = Join-Path $Work 'qtsvg'
Checkout 'https://github.com/qt/qtsvg.git' "v$($Pins.qt_version)" $Pins.qtsvg_commit $Svg
Build $Svg (Join-Path $Work 'qtsvg-x86') $Qt @("-DCMAKE_PREFIX_PATH=$Qt",'-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF')
$PySide = Join-Path $Work 'pyside'
Checkout 'https://github.com/pyside/pyside-setup.git' "v$($Pins.qt_version)" $Pins.pyside_commit $PySide
Import-BazzaltMsvc x64
$HostQt = Join-Path $Work 'qt-host'
Build $QtBase (Join-Path $Work 'qtbase-host') $HostQt @('-DQT_BUILD_TESTS=OFF','-DQT_BUILD_EXAMPLES=OFF','-DFEATURE_gui=OFF','-DFEATURE_widgets=OFF','-DFEATURE_network=OFF','-DFEATURE_openssl=OFF','-DFEATURE_icu=OFF')
& (Join-Path $PSScriptRoot 'get_llvm.ps1') -Version $Versions.llvm -Sha256 $Versions.llvm_sha256
$Llvm = Join-Path $Root 'toolchain/llvm'
$env:CLANG_INSTALL_DIR = $Llvm
$HostShiboken = Join-Path $Work 'shiboken-host'
Build (Join-Path $PySide 'sources/shiboken6') (Join-Path $Work 'shiboken-host-build') $HostShiboken @("-DCMAKE_PREFIX_PATH=$HostQt", "-DPython_EXECUTABLE=$HostPython",'-DSHIBOKEN_BUILD_LIBS=OFF','-DSHIBOKEN_BUILD_TOOLS=ON','-DBUILD_TESTS=OFF')
$env:PATH = (Join-Path $HostQt 'bin') + ';' + (Join-Path $Llvm 'bin') + ';' + $env:PATH
Import-BazzaltMsvc x86
$Bindings = Join-Path $Work 'bindings-x86'
Build $PySide (Join-Path $Work 'pyside-x86') $Bindings @("-DCMAKE_PREFIX_PATH=$Qt", "-DPython_EXECUTABLE=$Python", "-DQFP_SHIBOKEN_HOST_PATH=$HostShiboken", "-DQFP_QT_TARGET_PATH=$Qt", "-DPYTHON_SITE_PACKAGES=$Bindings/Lib/site-packages",'-DSHIBOKEN6TOOLS_SKIP_FIND_DEPENDENCIES=ON','-DSHIBOKEN_BUILD_TOOLS=OFF','-DSHIBOKEN_BUILD_LIBS=ON','-DFORCE_LIMITED_API=OFF','-DBUILD_TESTS=OFF','-DDISABLE_DOCSTRINGS=ON','-DMODULES=Core;Gui;Widgets;Network;OpenGL;OpenGLWidgets;PrintSupport;Svg;SvgWidgets;Test;Xml')
$Wheels = Join-Path $Work 'wheels'
Checked $HostPython @((Join-Path $PSScriptRoot 'package_pyside_win32.py'),'--prefix',$Bindings,'--qt',$Qt,'--source',$PySide,'--output',$Wheels,'--version',$Pins.qt_version)
Checked $Python @('-m','pip','install','--no-index','--find-links',$Wheels,"PySide6==$($Pins.qt_version)")
Checked $Python @('-m','pip','install','-r',(Join-Path $Root 'requirements-build.txt'),'pybind11==3.0.2')
$env:QT_QPA_PLATFORM = 'offscreen'
Checked $Python @('-c','from PySide6.QtWidgets import QApplication; from PySide6.QtSvg import QSvgRenderer; app=QApplication([]); assert QSvgRenderer(b"<svg xmlns=\"http://www.w3.org/2000/svg\"/>").isValid()')
if ($Stage -eq 'all') {
    Build-Filament
}
Write-Host "Win32 Python: $Python"
Write-Host "Win32 Filament: $(Join-Path $Work 'filament-x86')"
