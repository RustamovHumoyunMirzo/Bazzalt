"""Real cooker -> packages -> native player, without a Python game runtime."""
import json,os,subprocess,tempfile,unittest,zipfile
from unittest.mock import patch
from pathlib import Path
from Compiler.settings import DEFAULTS,ApplyPreset,Save
from Compiler.build import Build
from scripts.stage_game_runtime import Stage

ROOT=Path(__file__).resolve().parents[2]
@unittest.skipUnless(os.name=='nt' and (ROOT/'build/Editor/Release/BazzaltPlayer.exe').is_file() and (ROOT/'build/Editor/Release/BazzaltCooker.exe').is_file(),'Prebuilt native player/cooker are required')
class NativeBuildTests(unittest.TestCase):
    def test_cooked_game_loose_encrypted_and_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Документы';root.mkdir();runtime=Stage(ROOT/'build',root/'Runtime','x64');project=root/'Game.bproject';assets=root/'Assets';assets.mkdir()
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities: []\n',encoding='utf-8')
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            for preset,archive,format in (('low',False,'loose'),('low',False,'pak'),('medium',False,'pak'),('high',False,'pak'),('low',True,'pak')):
                settings=ApplyPreset(DEFAULTS,preset);settings['production']['name']='TestGame';settings['packaging']['archive']=archive;settings['packaging']['format']=format;Save(project,settings)
                with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):output=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
                self.assertFalse(list((root/'.bazzalt/Cache/Build/x64').glob('work-*')))
                # Obsolete products beside the current scene cache must never
                # enter subsequent cooks, even though they remain editor cache.
                caches=list((root/'.bazzalt/Cache').glob('*/*.bscene'))
                for cache in caches:(cache.parent/'obsolete-generation.bin').write_bytes(b'old data')
                if archive:
                    with zipfile.ZipFile(output) as zipped:self.assertIn('TestGame.exe',zipped.namelist());self.assertFalse(any(name.endswith(('.py','.pyd','.cpp','.lua')) for name in zipped.namelist()))
                else:
                    if format=='loose':self.assertFalse(list((output/'Data').rglob('obsolete-generation.bin')))
                    executable=output/'TestGame.exe'
                    with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):result=subprocess.run([str(executable),'--verify-package'],timeout=30)
                    self.assertEqual(result.returncode,0)
                    if format=='pak':
                        first=next(output.glob('*.bpak'));data=bytearray(first.read_bytes());data[-1]^=1;first.write_bytes(data)
                        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):result=subprocess.run([str(executable),'--verify-package'],timeout=30)
                        self.assertNotEqual(result.returncode,0)
            events=[]
            with patch('Compiler.build.Emit',side_effect=lambda stage,*args,**kwargs:events.append(stage)),patch('Compiler.build.RunLaunch',side_effect=lambda path:events.append('launched')):
                Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe',launch=True)
            self.assertLess(events.index('complete'),events.index('launched'));self.assertNotIn('error',events)
    @unittest.skipUnless(os.environ.get('BAZZALT_GAME_GPU_TEST')=='1','Opt in to real hidden-window rendering')
    def test_native_game_renders_and_runs_script_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);runtime=Stage(ROOT/'build',root/'Runtime','x64');project=root/'Game.bproject';assets=root/'Assets';assets.mkdir();entity='34567890-1234-4234-8234-123456789abc'
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities:\n  - UUID: "'+entity+'"\n    Parent: "00000000-0000-0000-0000-000000000000"\n    Name: Probe\n    Components: []\n',encoding='utf-8')
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            log=root/'lifecycle.txt'
            bindings=[dict(module=str(ROOT/'build/Release/BazzaltScriptLifecycleModule.dll'),entity=entity,type='LifecycleProbe',properties={'LogPath':str(log)})]
            for mode in ('windowed','maximized','borderless','fullscreen'):
                settings=ApplyPreset(DEFAULTS,'medium');settings['window']['mode']=mode;settings['intro']['enabled']=mode!='windowed';Save(project,settings)
                with patch('Compiler.build.CompileScripts',return_value=bindings):output=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
                result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],timeout=60);self.assertEqual(result.returncode,0,mode)
            text=log.read_text();self.assertIn('create:'+entity,text);self.assertIn('update:'+entity,text);self.assertIn('destroy:'+entity,text)
