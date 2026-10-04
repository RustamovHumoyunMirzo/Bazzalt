param(
    [string]$Version = "1.0.0",
    [string]$BundleDirectory = "dist/production/bazzalt_hub.dist",
    [string]$OutputDirectory = "dist/installer",
    [string]$InnoCompiler = ""
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BundleInput = if ([System.IO.Path]::IsPathRooted($BundleDirectory)) { $BundleDirectory } else { Join-Path $Root $BundleDirectory }
$OutputInput = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $Root $OutputDirectory }
$Bundle = [System.IO.Path]::GetFullPath($BundleInput)
$Output = [System.IO.Path]::GetFullPath($OutputInput)
$VersionFolder = "bazzalt_" + $Version.Replace('.', '_')

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must use major.minor.patch" }
$Required = @(
    (Join-Path $Bundle "BazzaltHub.exe"),
    (Join-Path $Bundle "versions/$VersionFolder/Bazzalt.exe"),
    (Join-Path $Bundle "versions/$VersionFolder/editor.json")
)
foreach ($Path in $Required) { if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing production file: $Path" } }
python (Join-Path $Root "scripts/compile_resources.py") --audit $Bundle
if ($LASTEXITCODE -ne 0) { throw "Installer input contains raw application resources. Rebuild the production bundle." }

if (-not $InnoCompiler) {
    $Command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($Command) { $InnoCompiler = $Command.Source }
}
if (-not $InnoCompiler) {
    $Candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6/ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6/ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs/Inno Setup 6/ISCC.exe")
    )
    $InnoCompiler = $Candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
}
if (-not $InnoCompiler) { throw "Inno Setup 6 was not found. Install it with: winget install JRSoftware.InnoSetup" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Script = Join-Path $Root "installer/BazzaltHub.iss"
& $InnoCompiler "/DAppVersion=$Version" "/DBundleDir=$Bundle" `
    "/DInstallerOutputDir=$Output" "/FBazzaltHub-Setup" $Script
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE" }

$Installer = Join-Path $Output "BazzaltHub-Setup.exe"
if (-not (Test-Path -LiteralPath $Installer -PathType Leaf)) { throw "Installer output was not created" }
Write-Host "Installer: $Installer"
