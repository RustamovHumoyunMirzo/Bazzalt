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

    def test_source_only_sync_preserves_asset_identity_and_removes_deleted_code(self):
        import hashlib,json
        from unittest.mock import patch
        from Compiler.remote_worker import Execute
        from Compiler.settings import Merge,Save
        from bazzalt.platforms import HostPlatform
        with tempfile.TemporaryDirectory() as directory:
            workspace=Path(directory);job=workspace/'job';job.mkdir();archive=job/'project.zip';identity='b'*64
            settings=Merge({});settings['target']['platform']=HostPlatform()
            def build(project,*args,**kwargs):
                output=project.parent/'bin/Game.zip';output.parent.mkdir(exist_ok=True);output.write_bytes(b'output');return output
            def transfer(files,full):
                with zipfile.ZipFile(archive,'w') as output:
                    output.writestr('Game.bproject','AssetDirectory: Assets\n');output.writestr('BuildSettings.json',json.dumps(settings))
                    for name in files:output.writestr('Assets/'+name,b'asset-or-code')
                request=job/'request.json';request.write_text(json.dumps(dict(protocol=1,project_id=identity,project='Game.bproject',sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),sync_assets=full)),encoding='utf-8')
                Execute(request)
            with patch('Compiler.build.Build',side_effect=build):
                transfer(['music.bin','music.bin.meta','Old.cpp','Old.cpp.meta'],True)
                transfer([],False)
            root=workspace/'projects'/identity/'Project/Assets'
            self.assertTrue((root/'music.bin').is_file());self.assertTrue((root/'music.bin.meta').is_file())
            self.assertFalse((root/'Old.cpp').exists());self.assertFalse((root/'Old.cpp.meta').exists())
