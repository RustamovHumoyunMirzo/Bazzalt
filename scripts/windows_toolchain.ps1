# Shared MSVC environment setup. Only changes the current build process.
function Import-BazzaltMsvc([ValidateSet('x64','x86')][string]$Architecture) {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $VsWhere)) { throw 'Visual Studio C++ Build Tools with x86/x64 compilers are required.' }
    $Installation = (& $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $Installation) { throw 'No Visual Studio C++ x86/x64 toolset was found.' }
    $VcVars = Join-Path $Installation 'VC/Auxiliary/Build/vcvarsall.bat'
    $Target = if ($Architecture -eq 'x86') { 'amd64_x86' } else { 'amd64' }
    $Environment = & $env:ComSpec /d /c "call `"$VcVars`" $Target >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "MSVC $Architecture environment setup failed." }
    foreach ($Line in $Environment) {
        $Pair = $Line -split '=', 2
        if ($Pair.Count -eq 2 -and $Pair[0] -and -not $Pair[0].StartsWith('=')) {
            [Environment]::SetEnvironmentVariable($Pair[0], $Pair[1], 'Process')
        }
    }
    if ($env:VSCMD_ARG_TGT_ARCH -ne $Architecture) { throw "MSVC selected the wrong target: $env:VSCMD_ARG_TGT_ARCH" }
}
