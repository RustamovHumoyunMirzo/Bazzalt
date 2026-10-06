# Compatibility entry point. Products are deliberately packaged independently.
param(
    [ValidateSet('editor','core','hub','all')][string]$Product = 'all',
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [string]$Version = '',
    [string]$BuildDirectory = 'build',
    [string]$OutputDirectory = 'dist/releases',
    [switch]$CreateInstaller
)
$ErrorActionPreference = 'Stop'
if ($Architecture -eq 'x86' -and $Product -ne 'core') {
    throw 'Win32 packaging is supported only with -Product core; Editor and Hub require x64.'
}
$Products = if ($Product -eq 'all') { @('core','editor','hub') } else { @($Product) }
foreach ($Item in $Products) {
    & (Join-Path $PSScriptRoot 'build_windows_release.ps1') -Product $Item -Architecture $Architecture -Version $Version -BuildDirectory $BuildDirectory -OutputDirectory $OutputDirectory
}
