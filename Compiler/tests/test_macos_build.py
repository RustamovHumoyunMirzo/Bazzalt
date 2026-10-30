"""Mac-only real native cook -> encrypted package -> player verification/render."""
import copy,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
from Compiler.build import Build
from Compiler.settings import DEFAULTS,Save
from scripts.stage_game_runtime import Stage
from Editor.scripting import ScriptCompiler,ScriptAttachments
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/os.environ.get('BAZZALT_MAC_TEST_BUILD','build')
ARCH=__import__('bazzalt.platforms',fromlist=['HostArchitecture']).HostArchitecture()

@unittest.skipUnless(sys.platform=='darwin' and (BUILD/'Editor/BazzaltPlayer').is_file(),'Native Mac build required')
class MacBuildTests(unittest.TestCase):
    def test_macos_encryption_compression_and_player(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)/'Игра';root.mkdir();assets=root/'Assets';assets.mkdir();project=root/'Game.bproject'
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Mac\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            entity='34567890-1234-4234-8234-123456789abc'
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities:\n  - UUID: "'+entity+'"\n    Parent: "00000000-0000-0000-0000-000000000000"\n    Name: Probe\n    Components: []\n')
            script=assets/'Probe.cpp';log=root/'lifecycle.txt'
            script.write_text('#include <Bazzalt/Script.h>\n#include <Bazzalt/Math.h>\n#include <fstream>\nCOMPONENT(MacProbe) { public: PROPERTY(std::string, LogPath, "")\nvoid OnCreate() override { std::ofstream(LogPath, std::ios::app) << "create\\n"; }\nvoid OnUpdate(float) override { std::ofstream(LogPath, std::ios::app) << "update\\n"; }\nvoid OnDestroy() override { std::ofstream(LogPath, std::ios::app) << "destroy\\n"; }\n};\n',encoding='utf-8')
            attachments=ScriptAttachments(root);attachments.Attach(entity,ScriptCompiler.Inspect(script));attachments.values['entities'][entity][0]['properties']['LogPath']=str(log);attachments.Save()
            (assets/'Payload.bin').write_bytes(b'Mac payload '*400000)
            runtime=Stage(BUILD,root/'Runtime',ARCH,platform='macos')
            settings=copy.deepcopy(DEFAULTS);settings['target'].update(platform='macos',architecture=ARCH);settings['intro']['duration_ms']=0;settings['security'].update(preset='medium',encryption='aes-256-gcm',encrypt_manifests=True)
            Save(project,settings);output=Build(project,runtime,BUILD/'Editor/BazzaltCooker');executable=output/'Contents/MacOS/Game'
            self.assertTrue(os.access(executable,os.X_OK));packages=list((output/'Contents/Resources').glob('*.bpak'));self.assertTrue(packages);self.assertLess(sum(p.stat().st_size for p in packages),1000000)
            self.assertTrue((output/'Contents/Resources/licenses').is_dir());self.assertFalse((output/'Contents/MacOS/licenses').exists())
            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=60);self.assertEqual(result.returncode,0,result.stderr)
            if os.environ.get('BAZZALT_MAC_GPU_TEST')=='1':
                result=subprocess.run([str(executable),'--smoke-test'],capture_output=True,timeout=90);self.assertEqual(result.returncode,0,result.stderr)
                text=log.read_text();self.assertIn('create',text);self.assertIn('update',text);self.assertIn('destroy',text)
            if os.environ.get('BAZZALT_MAC_GPU_TEST')=='1':
                # Provision a second build only on this player's first real run.
                from unittest.mock import patch
                from Compiler.settings import Load
                settings=Load(project);settings['platforms']['macos']['key_storage']='keychain';settings['platforms']['macos']['bundle_identifier']='org.bazzalt.ci.test'
                Save(project,settings)
                key='a7'*32
                with patch.dict(os.environ,{'BAZZALT_GAME_KEY':key}):output=Build(project,runtime,BUILD/'Editor/BazzaltCooker')
                executable=output/'Contents/MacOS/Game';boot=json.loads((executable.parent/'game.bootstrap').read_text());service=boot['key_service'];account=boot['build_id']
                lookup=['/usr/bin/security','find-generic-password','-s',service,'-a',account]
                self.assertNotEqual(subprocess.run(lookup,capture_output=True).returncode,0,'Verification must not provision builder Keychain')
                environment=dict(os.environ);environment['BAZZALT_GAME_KEY']=key
                try:
                    result=subprocess.run([str(executable),'--smoke-test'],env=environment,capture_output=True,timeout=90);self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(subprocess.run(lookup,capture_output=True).returncode,0)
                    environment.pop('BAZZALT_GAME_KEY',None)
                    result=subprocess.run([str(executable),'--smoke-test'],env=environment,capture_output=True,timeout=90);self.assertEqual(result.returncode,0,result.stderr)
                finally:subprocess.run(['/usr/bin/security','delete-generic-password','-s',service,'-a',account],capture_output=True)
            manifest=output/'Contents/Resources/game.json';data=bytearray(manifest.read_bytes());data[-1]^=1;manifest.write_bytes(data)
            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=60);self.assertNotEqual(result.returncode,0)
