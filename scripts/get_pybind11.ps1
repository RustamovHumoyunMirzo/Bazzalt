param([string]$Destination = "$PSScriptRoot\..\deps\pybind11")
$ErrorActionPreference = "Stop"
$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path (Join-Path $Destination "CMakeLists.txt")) { Write-Host "pybind11 already exists at $Destination"; exit 0 }
git clone --depth 1 --branch v3.0.1 https://github.com/pybind/pybind11.git $Destination
