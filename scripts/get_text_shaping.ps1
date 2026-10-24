$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
foreach ($Dependency in @(
    @{Name='harfbuzz';Tag='11.5.0';Commit='bf8929fbfb623703cf1522b372cab80002c17c95';Url='https://github.com/harfbuzz/harfbuzz.git'},
    @{Name='sheenbidi';Tag='v3.0.0';Commit='cfe430e7375a7845b679adae9d51dac6deaa8858';Url='https://github.com/Tehreer/SheenBidi.git'}
)) {
    $Directory=Join-Path $Root ('deps/'+$Dependency.Name)
    if (-not (Test-Path $Directory)) {
        git clone --depth 1 --branch $Dependency.Tag $Dependency.Url $Directory
        if ($LASTEXITCODE -ne 0) { throw ('Download failed: '+$Dependency.Name) }
    }
    $Actual=git -C $Directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $Actual.Trim() -ne $Dependency.Commit) { throw ('Dependency integrity check failed: '+$Dependency.Name) }
}
