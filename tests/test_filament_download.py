"""Verify download retries and integrity gates without fetching the renderer SDK."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "scripts/get_filament.ps1"


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell is required")
class FilamentDownloadTests(unittest.TestCase):
    def RunDownload(self, failures, bad_hash=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "scripts").mkdir()
            (root / "deps").mkdir()
            source = SOURCE.read_text(encoding="utf-8")
            script = root / "scripts/get_filament.ps1"
            script.write_text(source, encoding="utf-8")
            expected = re.search(r'\$ExpectedSha256 = "([a-f0-9]{64})"', source).group(1)
            digest = "0" * 64 if bad_hash else expected
            quoted = root.as_posix().replace("'", "''")
            # Functions shadow OS/download commands only inside this test process.
            command = f"""
$ErrorActionPreference = 'Stop'
$global:Downloads = 0
$global:Extractions = 0
$global:Delays = @()
$global:Root = '{quoted}'
function global:Invoke-WebRequest {{
    param($Uri, $OutFile, $TimeoutSec)
    $global:Downloads++
    [IO.File]::WriteAllText($OutFile, 'partial or downloaded archive')
    if ($global:Downloads -le {failures}) {{ throw 'Simulated HTTP 503' }}
}}
function global:Start-Sleep {{ param($Seconds) $global:Delays += $Seconds }}
function global:Get-FileHash {{ param($LiteralPath, $Algorithm) [pscustomobject]@{{Hash='{digest}'}} }}
function global:tar {{
    $global:Extractions++
    $target = Join-Path $global:Root 'deps/filament/include/filament'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    [IO.File]::WriteAllText((Join-Path $target 'Engine.h'), 'verified test SDK')
    $global:LASTEXITCODE = 0
}}
$failure = ''
try {{ & (Join-Path $global:Root 'scripts/get_filament.ps1') }} catch {{ $failure = $_.Exception.Message }}
@{{downloads=$global:Downloads; extractions=$global:Extractions; delays=@($global:Delays);
   failure=$failure; archive=(Test-Path (Join-Path $global:Root 'deps/filament-v1.77.0-windows.tgz'))}} |
    ConvertTo-Json -Compress
"""
            result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-NonInteractive", "-Command", command],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout.splitlines()[-1])

    def test_transient_download_failure_recovers_before_verified_extraction(self):
        result = self.RunDownload(2)
        self.assertEqual(result["downloads"], 3)
        self.assertEqual(result["extractions"], 1)
        self.assertEqual(result["delays"], [2, 4])
        self.assertEqual(result["failure"], "")
        self.assertFalse(result["archive"])

    def test_persistent_failure_stops_and_cleans_partial_archive(self):
        result = self.RunDownload(10)
        self.assertEqual(result["downloads"], 4)
        self.assertEqual(result["extractions"], 0)
        self.assertEqual(result["delays"], [2, 4, 6])
        self.assertIn("HTTP 503", result["failure"])
        self.assertFalse(result["archive"])

    def test_integrity_failure_never_extracts_or_retries(self):
        result = self.RunDownload(0, bad_hash=True)
        self.assertEqual(result["downloads"], 1)
        self.assertEqual(result["extractions"], 0)
        self.assertIn("integrity check failed", result["failure"])
        self.assertFalse(result["archive"])


if __name__ == "__main__":
    unittest.main()
