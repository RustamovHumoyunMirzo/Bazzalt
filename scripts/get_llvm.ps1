param([string]$Version='22.1.0', [string]$Sha256='')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Destination=Join-Path $Root 'toolchain/llvm'
if(Test-Path (Join-Path $Destination 'bin/clang++.exe')) { Write-Host "LLVM already installed: $Destination"; exit 0 }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid LLVM release version' }
if ($Sha256) {
    # Pinned builds need no GitHub API request (shared CI IPs are rate-limited).
    $DownloadUrl="https://github.com/llvm/llvm-project/releases/download/llvmorg-$Version/clang+llvm-$Version-x86_64-pc-windows-msvc.tar.xz"
} else {
    $Headers=@{Accept='application/vnd.github+json'}
    $Token=if($env:GH_TOKEN){$env:GH_TOKEN}else{$env:GITHUB_TOKEN}
    if($Token){$Headers.Authorization="Bearer $Token"}
    $Release=Invoke-RestMethod "https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-$Version" -Headers $Headers
    $Asset=$Release.assets | Where-Object { $_.name -match '^clang\+llvm-.*x86_64-pc-windows-msvc\.tar\.xz$' } | Select-Object -First 1
    if(-not $Asset) { throw "LLVM $Version Windows archive was not found in the official release" }
    $DownloadUrl=$Asset.browser_download_url
    if($Asset.digest -match '^sha256:([a-fA-F0-9]{64})$') { $Sha256=$Matches[1] }
}
if($Sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Official asset has no SHA-256 digest. Supply a verified -Sha256; unverified archives are not accepted.' }
$Parent=Join-Path $Root 'toolchain'
New-Item -ItemType Directory -Force -Path $Parent | Out-Null
$Stage=Join-Path $Parent ('.llvm-stage-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Stage | Out-Null
try {
    $Archive=Join-Path $Stage 'llvm.tar.xz'
    Invoke-WebRequest $DownloadUrl -OutFile $Archive
    if((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $Sha256) { throw 'LLVM archive SHA-256 mismatch' }
    tar -xf $Archive -C $Stage
    if($LASTEXITCODE -ne 0) { throw 'LLVM extraction failed' }
    $Extracted=Get-ChildItem -LiteralPath $Stage -Directory | Select-Object -First 1
    if(-not $Extracted -or -not (Test-Path (Join-Path $Extracted.FullName 'bin/clang++.exe'))) { throw 'Unexpected LLVM archive layout' }
    Move-Item -LiteralPath $Extracted.FullName -Destination $Destination
} finally {
    $ResolvedStage=[IO.Path]::GetFullPath($Stage)
    if(-not $ResolvedStage.StartsWith([IO.Path]::GetFullPath($Parent)+[IO.Path]::DirectorySeparatorChar)) { throw 'Unsafe temporary cleanup target' }
    if(Test-Path -LiteralPath $ResolvedStage) { Remove-Item -LiteralPath $ResolvedStage -Recurse -Force }
}
Write-Host "Installed verified development LLVM toolchain: $Destination"
