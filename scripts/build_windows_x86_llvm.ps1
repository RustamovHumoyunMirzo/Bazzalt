param([ValidateRange(1,32)][int]$Parallel = 2, [switch]$AllowLocalBuild)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -and -not $AllowLocalBuild) {
    throw 'Large compiler builds belong in GitHub Actions. Local builds require explicit -AllowLocalBuild opt-in.'
}
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Versions = Get-Content (Join-Path $Root 'releases/versions.json') -Raw | ConvertFrom-Json
$Pins = Get-Content (Join-Path $Root 'releases/windows-x86.json') -Raw | ConvertFrom-Json
$Work = Join-Path $Root 'build/win32-llvm'
$Source = Join-Path $Work 'source'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $Source '.git'))) {
    git clone --depth 1 --branch "llvmorg-$($Versions.llvm)" https://github.com/llvm/llvm-project.git $Source
    if ($LASTEXITCODE -ne 0) { throw 'LLVM source download failed' }
}
$Commit = (& git -C $Source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Commit -ne $Pins.llvm_commit) { throw 'LLVM source integrity check failed' }
. (Join-Path $PSScriptRoot 'windows_toolchain.ps1')
Import-BazzaltMsvc x86
$Build = Join-Path $Work 'build'
$Install = Join-Path $Work 'llvm'
cmake -S (Join-Path $Source 'llvm') -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$Install" `
    '-DLLVM_ENABLE_PROJECTS=clang;lld' -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF `
    -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF `
    -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_DEFAULT_TARGET_TRIPLE=i686-pc-windows-msvc `
    -DLLVM_HOST_TRIPLE=i686-pc-windows-msvc '-DCMAKE_EXE_LINKER_FLAGS=/LARGEADDRESSAWARE'
if ($LASTEXITCODE -ne 0) { throw 'LLVM Win32 configuration failed' }
cmake --build $Build --parallel $Parallel --target clang lld
if ($LASTEXITCODE -ne 0) { throw 'LLVM Win32 compiler build failed' }
cmake --install $Build --component clang
if ($LASTEXITCODE -ne 0) { throw 'Clang installation failed' }
cmake --install $Build --component clang-resource-headers
if ($LASTEXITCODE -ne 0) { throw 'Clang resource header installation failed' }
cmake --install $Build --component lld
if ($LASTEXITCODE -ne 0) { throw 'LLD installation failed' }
$Compiler = Join-Path $Install 'bin/clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'LLVM installation has no Clang++ driver' }
& $Compiler --version
if ($LASTEXITCODE -ne 0) { throw 'The generated Win32 compiler cannot run' }
python (Join-Path $PSScriptRoot 'release_artifacts.py') --root $Install --product llvm --version $Versions.llvm --architecture x86 `
    --output (Join-Path $Root "dist/releases/Bazzalt-llvm-$($Versions.llvm)-windows-x86.zip")
if ($LASTEXITCODE -ne 0) { throw 'LLVM package contains non-Win32 binaries' }
