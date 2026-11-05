import subprocess
import sys
import unittest
from unittest.mock import patch
from scripts.run_macos_native_tests import Run


class NativeCheckTimeoutTests(unittest.TestCase):
    def test_preserves_success_and_failure(self):
        self.assertEqual(Run([sys.executable, "-c", "pass"], timeout=10), 0)
        self.assertEqual(Run([sys.executable, "-c", "raise SystemExit(7)"], timeout=10), 7)

    def test_hung_check_is_terminated_and_fails(self):
        processes=[]
        original=subprocess.Popen
        def Launch(*args,**kwargs):
            process=original(*args,**kwargs);processes.append(process);return process
        with patch("scripts.run_macos_native_tests.subprocess.Popen", side_effect=Launch):
            self.assertEqual(Run([sys.executable, "-c", "import time; time.sleep(60)"], timeout=.1), 124)
            self.assertIsNotNone(processes[0].poll())  # Timed-out child was reaped.
