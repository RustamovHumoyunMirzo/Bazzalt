param([string]$Version="22.1.0")
$ErrorActionPreference="Stop"
$Root=(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Destination=Join-Path $Root "toolchain/llvm"
if(Test-Path (Join-Path $Destination "bin/clang++.exe")){Write-Host "LLVM already installed: $Destination";exit 0}
$Release=Invoke-RestMethod "https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-$Version"
$Asset=$Release.assets|Where-Object{$_.name -match '^clang\+llvm-.*x86_64-pc-windows-msvc\.tar\.xz$'}|Select-Object -First 1
if(-not $Asset){throw "LLVM $Version Windows archive was not found in the official release"}
$Archive=Join-Path ([IO.Path]::GetTempPath()) $Asset.name
Invoke-WebRequest $Asset.browser_download_url -OutFile $Archive
$Stage=Join-Path $Root "toolchain/.llvm-stage"
if(Test-Path $Stage){Remove-Item -LiteralPath $Stage -Recurse -Force}
New-Item -ItemType Directory -Force -Path $Stage|Out-Null
tar -xf $Archive -C $Stage
$Extracted=Get-ChildItem $Stage -Directory|Select-Object -First 1
New-Item -ItemType Directory -Force -Path (Split-Path $Destination)|Out-Null
Move-Item -LiteralPath $Extracted.FullName -Destination $Destination
Remove-Item -LiteralPath $Stage -Recurse -Force
Remove-Item -LiteralPath $Archive -Force
Write-Host "Installed private LLVM toolchain: $Destination"
