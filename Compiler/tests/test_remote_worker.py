import io
from pathlib import Path
import tempfile
import unittest
import zipfile
from Compiler.remote_worker import Extract,Info

class RemoteWorkerTests(unittest.TestCase):
    def test_protocol_and_bounded_extract(self):
        self.assertEqual(Info()['protocol'],1)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);archive=root/'input.zip'
            for name in ('../escape','/absolute','Assets/../../escape'):
                with zipfile.ZipFile(archive,'w') as output:output.writestr(name,b'bad')
                with self.assertRaises(ValueError):Extract(archive,root/'project')
            with zipfile.ZipFile(archive,'w') as output:output.writestr('Assets/Cube.cpp',b'code')
            Extract(archive,root/'project');self.assertEqual((root/'project/Assets/Cube.cpp').read_bytes(),b'code')

    def test_parallel_sync_is_rejected_before_any_project_changes(self):
        import json
        from Compiler.build import BuildLock
        from Compiler.remote_worker import Execute
        with tempfile.TemporaryDirectory() as directory:
            workspace=Path(directory);job=workspace/'job';job.mkdir();identity='a'*64
            base=workspace/'projects'/identity;base.mkdir(parents=True)
            request=job/'request.json';request.write_text(json.dumps(dict(protocol=1,project_id=identity,clean=True)),encoding='utf-8')
            with BuildLock(base),self.assertRaisesRegex(ValueError,'Another build'):
                Execute(request)
            self.assertFalse((base/'Project').exists())
