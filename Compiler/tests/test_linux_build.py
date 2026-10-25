"""Linux-only real native cook -> encrypted package -> player verification/render."""
import copy,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
from Compiler.build import Build
from Compiler.settings import DEFAULTS,Save
from scripts.stage_game_runtime import Stage
from Editor.scripting import ScriptCompiler,ScriptAttachments
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/os.environ.get('BAZZALT_LINUX_TEST_BUILD','build')
ARCH=os.environ.get('BAZZALT_LINUX_TEST_ARCH','x64')

@unittest.skipUnless(sys.platform.startswith('linux') and (BUILD/'Editor/BazzaltPlayer').is_file(),'Native Linux build required')
class LinuxBuildTests(unittest.TestCase):
    def test_linux_encryption_compression_and_player(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)/'Игра';root.mkdir();assets=root/'Assets';assets.mkdir();project=root/'Game.bproject'
            project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Linux\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8')
            entity='34567890-1234-4234-8234-123456789abc'
            (assets/'Main.bscene').write_text('FormatVersion: 1\nSceneUUID: "12345678-1234-4234-8234-123456789abc"\nEntities:\n  - UUID: "'+entity+'"\n    Parent: "00000000-0000-0000-0000-000000000000"\n    Name: Probe\n    Components: []\n')
            script=assets/'Probe.cpp';log=root/'lifecycle.txt'
            script.write_text('#include <Bazzalt/Script.h>\n#include <Bazzalt/Math.h>\n#include <fstream>\nCOMPONENT(LinuxProbe) { public: PROPERTY(std::string, LogPath, "")\nvoid OnCreate() override { std::ofstream(LogPath, std::ios::app) << "create\\n"; }\nvoid OnUpdate(float) override { std::ofstream(LogPath, std::ios::app) << "update\\n"; }\nvoid OnDestroy() override { std::ofstream(LogPath, std::ios::app) << "destroy\\n"; }\n};\n',encoding='utf-8')
            attachments=ScriptAttachments(root);attachments.Attach(entity,ScriptCompiler.Inspect(script));attachments.values['entities'][entity][0]['properties']['LogPath']=str(log);attachments.Save()
            (assets/'Payload.bin').write_bytes(b'Linux payload '*400000)
            runtime=Stage(BUILD,root/'Runtime',ARCH,platform='linux')
            settings=copy.deepcopy(DEFAULTS);settings['target'].update(platform='linux',architecture=ARCH);settings['intro']['duration_ms']=0;settings['security'].update(preset='medium',encryption='aes-256-gcm',encrypt_manifests=True)
            Save(project,settings);output=Build(project,runtime,BUILD/'Editor/BazzaltCooker');executable=output/'Game'
            self.assertTrue(os.access(executable,os.X_OK));self.assertLess(sum(p.stat().st_size for p in output.glob('*.bpak')),1000000)
            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=60);self.assertEqual(result.returncode,0,result.stderr)
            if os.environ.get('DISPLAY'):
                result=subprocess.run([str(executable),'--smoke-test'],capture_output=True,timeout=90);self.assertEqual(result.returncode,0,result.stderr)
                text=log.read_text();self.assertIn('create',text);self.assertIn('update',text);self.assertIn('destroy',text)
            manifest=output/'game.json';data=bytearray(manifest.read_bytes());data[-1]^=1;manifest.write_bytes(data)
            result=subprocess.run([str(executable),'--verify-package'],capture_output=True,timeout=60);self.assertNotEqual(result.returncode,0)
