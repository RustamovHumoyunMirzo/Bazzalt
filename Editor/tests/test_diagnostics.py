import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class DiagnosticsTests(unittest.TestCase):
    def test_native_python_and_fault_trace_output_survive_gui_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            code="""import os,sys,faulthandler
from pathlib import Path
from Editor import diagnostics
assert diagnostics.StartDiagnostics(Path(sys.argv[1]))
print('python output')
os.write(2,b'native stderr marker\\n')
faulthandler.dump_traceback(file=diagnostics._Log)
"""
            result=subprocess.run([sys.executable,"-c",code,directory],capture_output=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            path=Path(directory)/"editor-runtime.log"
            content=path.read_text(encoding="utf-8")
            self.assertIn("python output",content)
            self.assertIn("native stderr marker",content)
            self.assertIn("Current thread",content)
            result=subprocess.run([sys.executable,"-c",code,directory],capture_output=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual((Path(directory)/"editor-runtime.previous.log").read_text(encoding="utf-8"),content)
