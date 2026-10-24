"""Real cooker -> packages -> native player, without a Python game runtime."""
import json,os,subprocess,tempfile,unittest,zipfile,uuid,struct,sys
from unittest.mock import patch
from pathlib import Path
from Compiler.settings import DEFAULTS,ApplyPreset,Save
from Compiler.build import Build
from scripts.stage_game_runtime import Stage

ROOT=Path(__file__).resolve().parents[2]
@unittest.skipUnless(os.name=='nt' and (ROOT/'build/Editor/Release/BazzaltPlayer.exe').is_file() and (ROOT/'build/Editor/Release/BazzaltCooker.exe').is_file(),'Prebuilt native player/cooker are required')
class NativeBuildTests(unittest.TestCase):
    def test_cli_cancellation_with_open_control_pipe_exits_cleanly(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);runtime=Stage(ROOT/'build',root/'Runtime','x64');project=root/'Game.bproject';assets=root/'Assets';assets.mkdir()
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities: []\n')
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n')
            command=[sys.executable,'-m','Compiler.build','--project',str(project),'--runtime',str(runtime),'--cooker',str(ROOT/'build/Editor/Release/BazzaltCooker.exe')]
            process=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                process.stdin.write(b'cancel\n');process.stdin.flush();self.assertEqual(process.wait(timeout=30),2)
                self.assertIn(b'"stage": "cancelled"',process.stdout.read());self.assertNotIn(b'Fatal Python error',process.stderr.read());self.assertFalse((root/'bin').exists())
            finally:
                if process.poll() is None:process.kill();process.wait()
                process.stdin.close();process.stdout.close();process.stderr.close()
    def test_cooked_game_loose_encrypted_and_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Документы';root.mkdir();runtime=Stage(ROOT/'build',root/'Runtime','x64');project=root/'Game.bproject';assets=root/'Assets';assets.mkdir()
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities: []\n',encoding='utf-8')
            (assets/'Compressed.bin').write_bytes(b'Compressible game payload\n'*200000)
            (assets/'Duplicate.bin').write_bytes((assets/'Compressed.bin').read_bytes())
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            for preset,archive,format in (('low',False,'loose'),('low',False,'pak'),('medium',False,'pak'),('high',False,'pak'),('file',False,'pak'),('dpapi_user',False,'pak'),('plain_manifest',False,'pak'),('low',True,'pak')):
                settings=ApplyPreset(DEFAULTS,'medium' if preset in ('file','dpapi_user','plain_manifest') else preset)
                if preset in ('file','dpapi_user'):settings['security']['key_storage']=preset;settings['security']['preset']='custom'
                if preset=='plain_manifest':settings['security']['encrypt_manifests']=False;settings['security']['preset']='custom'
                settings['production']['name']='TestGame';settings['packaging']['archive']=archive;settings['packaging']['format']=format;Save(project,settings)
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
                    original_player=executable.read_bytes();modified_player=bytearray(original_player);modified_player[32]^=1;executable.write_bytes(modified_player)
                    with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                    self.assertNotEqual(result.returncode,0);self.assertIn(b'hash mismatch',result.stderr);executable.write_bytes(original_player)
                    if preset=='medium':
                        # Simulate the PE certificate-table layout produced by
                        # signing, not a trusted publisher certificate itself.
                        original=executable.read_bytes();signed=bytearray(original);padding=(-len(signed))%8;signed.extend(bytes(padding));start=len(signed);signed.extend(struct.pack('<IHH',16,0x200,2)+bytes(8))
                        pe=struct.unpack_from('<I',signed,60)[0];optional=pe+24;magic=struct.unpack_from('<H',signed,optional)[0];directory=optional+(96 if magic==0x10b else 112)+32
                        struct.pack_into('<II',signed,directory,start,16);executable.write_bytes(signed)
                        result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                        self.assertEqual(result.returncode,0,result.stderr)
                        from Compiler.pe_resources import StripTemplateSignature
                        StripTemplateSignature(executable);self.assertEqual(executable.stat().st_size,len(original)+padding)
                        executable.write_bytes(original)
                    if format=='pak' and preset=='low':
                        sessions=root/'PlayerTemp';sessions.mkdir(exist_ok=True)
                        stale=sessions/('BazzaltGame-{'+str(uuid.uuid4()).upper()+'}');stale.mkdir();(stale/'.bazzalt-session').write_bytes(b'BZSESSION002');(stale/'private.bin').write_bytes(b'old plaintext')
                        active=sessions/('BazzaltGame-{'+str(uuid.uuid4()).upper()+'}');active.mkdir();(active/'.bazzalt-session').write_bytes(b'BZSESSION002')
                        unowned=sessions/('BazzaltGame-{'+str(uuid.uuid4()).upper()+'}');unowned.mkdir();(unowned/'keep').touch()
                        with (active/'.bazzalt-session').open('rb') as lock,patch.dict(os.environ,{'TEMP':str(sessions),'TMP':str(sessions)}):
                            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                        self.assertEqual(result.returncode,0,result.stderr);self.assertFalse(stale.exists());self.assertTrue(active.exists());self.assertTrue(unowned.exists())
                    if settings['security']['encrypt_manifests']:
                        self.assertTrue((output/'game.json').read_bytes().startswith(b'BZENC003'));self.assertTrue((output/'game.integrity').read_bytes().startswith(b'BZENC003'));self.assertNotIn(b'Main.bscene',(output/'game.integrity').read_bytes())
                    if settings['security']['encryption']!='none':self.assertFalse(any(b'Assets/Main.bscene' in path.read_bytes() for path in output.glob('*.bpak')))
                    if preset=='high':
                        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'02'*32}):result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                        self.assertNotEqual(result.returncode,0);self.assertNotIn(b'020202',result.stderr)
                    for target in (output/'game.bootstrap',output/'game.json',output/'game.integrity',output/'game.signature'):
                        original=target.read_bytes();changed=bytearray(original);changed[-1]^=1;target.write_bytes(changed)
                        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                        self.assertNotEqual(result.returncode,0);target.write_bytes(original)
                    if format=='pak':
                        self.assertLess(sum(p.stat().st_size for p in output.glob('*.bpak')),1024*1024)
                        first=next(output.glob('*.bpak'));data=bytearray(first.read_bytes());data[-1]^=1;first.write_bytes(data)
                        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'01'*32}):result=subprocess.run([str(executable),'--verify-package'],timeout=30)
                        self.assertNotEqual(result.returncode,0)
                        if preset=='low':
                            # A valid inventory alone must not make malformed
                            # compressed data acceptable to the bounded reader.
                            import hashlib
                            from Compiler.pak import HEADER,MAGIC
                            from Compiler.integrity import Seal
                            path=b'bad.bin';body=b'not a compressed stream';header=HEADER.pack(len(path),len(body),1024,0,1024,2)
                            first.write_bytes(MAGIC+header+path+bytes(28)+hashlib.sha256(body).digest()+body)
                            Seal(output,executable)
                            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                            self.assertNotEqual(result.returncode,0);self.assertIn(b'compressed',result.stderr)
                            # Independently signed decoded-file hashes must also
                            # be enforced after successful decompression.
                            first.write_bytes(bytes(data[:-1])+bytes([data[-1]^1]))
                            Seal(output,executable,content={'Game.bproject':dict(size=0,sha256='0'*64)})
                            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=30)
                            self.assertNotEqual(result.returncode,0);self.assertIn(b'hash mismatch',result.stderr)
            events=[]
            with patch('Compiler.build.Emit',side_effect=lambda stage,*args,**kwargs:events.append(stage)),patch('Compiler.build.RunLaunch',side_effect=lambda path:events.append('launched')):
                Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe',launch=True)
            self.assertLess(events.index('complete'),events.index('launched'));self.assertNotIn('error',events)
            settings['packaging']['format']='loose';settings['packaging']['include_assets']='Assets/Main.bscene';Save(project,settings)
            filtered=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
            with zipfile.ZipFile(filtered) as zipped:
                self.assertIn('Data/Assets/Main.bscene',zipped.namelist());self.assertNotIn('Data/Assets/Compressed.bin',zipped.namelist());self.assertNotIn('Data/Assets/Duplicate.bin',zipped.namelist())
            import hashlib,threading
            from Compiler.build import BuildCancelled
            archive_path=root/'bin/TestGame-windows-x64.zip';before=hashlib.sha256(archive_path.read_bytes()).digest();cancel=threading.Event()
            def stop_scripts(*args):cancel.set();return []
            with patch('Compiler.build.CompileScripts',side_effect=stop_scripts),self.assertRaises(BuildCancelled):Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe',cancel=cancel.is_set)
            self.assertEqual(before,hashlib.sha256(archive_path.read_bytes()).digest());self.assertFalse(list((root/'.bazzalt/Cache/Build/x64').glob('work-*')))
    @unittest.skipUnless(os.environ.get('BAZZALT_GAME_GPU_TEST')=='1','Opt in to real hidden-window rendering')
    def test_native_game_renders_and_runs_script_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);runtime=Stage(ROOT/'build',root/'Runtime','x64');project=root/'Game.bproject';assets=root/'Assets';assets.mkdir();entity='34567890-1234-4234-8234-123456789abc'
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities:\n  - UUID: "'+entity+'"\n    Parent: "00000000-0000-0000-0000-000000000000"\n    Name: Probe\n    Components: []\n',encoding='utf-8')
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            log=root/'lifecycle.txt'
            application_id='45678901-1234-4234-8234-123456789abc'
            (assets/'Subsystem.cpp').write_text('#include <Bazzalt/Subsystem.h>\nSUBSYSTEM(SubsystemProbe) { public: void OnRegister() override {} };\n',encoding='utf-8')
            (assets/'Subsystem.cpp.meta').write_text(f'FormatVersion: 1\nUUID: "{application_id}"\nImporter: Bazzalt.Raw\nImporterVersion: 1\n',encoding='utf-8')
            project.write_text(project.read_text(encoding='utf-8').replace('Properties: {}','Properties:\n  application.subsystems: '+json.dumps(json.dumps([dict(asset=application_id,level=0)]))),encoding='utf-8')
            bindings=[dict(module=str(ROOT/'build/Release/BazzaltScriptLifecycleModule.dll'),entity=entity,type='LifecycleProbe',properties={'LogPath':str(log)})]
            bindings.append(dict(module=str(ROOT/'build/Release/BazzaltSubsystemProbe.dll'),entity='@subsystem:'+application_id,type='SubsystemProbe',properties={}))
            for mode in ('windowed','maximized','borderless','fullscreen'):
                settings=ApplyPreset(DEFAULTS,'medium');settings['window']['mode']=mode;settings['intro']['enabled']=mode!='windowed';Save(project,settings)
                with patch('Compiler.build.CompileScripts',return_value=bindings):output=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
                result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],timeout=60,capture_output=True);self.assertEqual(result.returncode,0,(mode,result.stderr))
                events=result.stdout.decode('utf-8',errors='replace')
                sequence=('OnBoot','OnBeforeWindowCreate','OnInit','OnLoad','OnSceneLoaded','OnPostWindowCreate','OnPlay','OnPreUpdate','OnLateUpdate','OnPreRender','OnRender','OnPostRender','OnPreShutdown','OnShutdown')
                positions=[events.index('SUBSYSTEM:'+name) for name in sequence];self.assertEqual(positions,sorted(positions),events)
            text=log.read_text();self.assertIn('create:'+entity,text);self.assertIn('update:'+entity,text);self.assertIn('destroy:'+entity,text)
            registrations=[dict(asset=application_id,level=4)]
            text=project.read_text(encoding='utf-8');prefix=text[:text.index('Properties:')]
            project.write_text(prefix+'Properties:\n  application.subsystems: '+json.dumps(json.dumps(registrations))+'\n',encoding='utf-8')
            with patch('Compiler.build.CompileScripts',return_value=bindings):output=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
            with patch.dict(os.environ,{'BAZZALT_SUBSYSTEM_PROBE':'lower'}):result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],timeout=60,capture_output=True)
            self.assertNotEqual(result.returncode,0);self.assertIn(b'lower-level event',result.stderr)
            project.write_text(prefix+'Properties:\n  application.subsystems: '+json.dumps(json.dumps([dict(asset=application_id,level=0)]))+'\n',encoding='utf-8')
            with patch('Compiler.build.CompileScripts',return_value=bindings):output=Build(project,runtime,ROOT/'build/Editor/Release/BazzaltCooker.exe')
            with patch.dict(os.environ,{'BAZZALT_SUBSYSTEM_PROBE':'early'}):result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],timeout=60,capture_output=True)
            self.assertNotEqual(result.returncode,0);self.assertIn(b'service is not initialized',result.stderr)
            with patch.dict(os.environ,{'BAZZALT_SUBSYSTEM_PROBE':'attach'}):result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],timeout=60,capture_output=True)
            self.assertNotEqual(result.returncode,0);self.assertIn(b'Registered subsystem cannot be attached',result.stderr)
