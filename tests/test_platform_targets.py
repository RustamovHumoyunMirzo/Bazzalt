import copy,os,stat,struct,subprocess,sys,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
from Compiler.settings import DEFAULTS,Validate
from bazzalt.platforms import TargetError
from scripts.release_artifacts import Architecture,Entry,Audit
from Launcher.downloads import Extract

class PlatformTargetsTests(unittest.TestCase):
    def test_staging_cli_resolves_shared_package_outside_repository(self):
        script=Path(__file__).resolve().parents[1]/'scripts/stage_game_runtime.py'
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            result=subprocess.run([sys.executable,'-E',str(script),'--build',str(root/'missing-build'),'--output',str(root/'runtime'),'--architecture','x64','--platform','windows'],cwd=root,capture_output=True,text=True)
            # A missing prebuilt player is rejected after resolving shared imports.
            self.assertNotEqual(result.returncode,0)
            self.assertIn('BazzaltPlayer.exe',result.stderr)
            self.assertNotIn('ModuleNotFoundError',result.stderr)

    def test_native_target_support_and_explicit_rejection(self):
        with patch('bazzalt.platforms.HostPlatform',return_value='linux'),patch('bazzalt.platforms.platform.machine',return_value='x86_64'):
            self.assertEqual(TargetError({'platform':'linux','architecture':'x64'}),'')
            self.assertIn('Windows',TargetError({'platform':'windows','architecture':'x64','build_method':'native'}))
            self.assertEqual(TargetError({'platform':'windows','architecture':'x64','build_method':'zig'}),'')
            self.assertEqual(TargetError({'platform':'linux','architecture':'x86'}),'')
            self.assertEqual(TargetError({'platform':'linux','architecture':'arm64','build_method':'zig'}),'')
            self.assertIn('Unsupported',TargetError({'platform':'linux','architecture':'arm32'}))
            self.assertIn('protection',TargetError({'platform':'linux','architecture':'x64','key_storage':'dpapi_user'}))
        value=copy.deepcopy(DEFAULTS);value['target']['platform']='linux';self.assertEqual(Validate(value)['target']['platform'],'linux')
    def test_elf_inspection_and_linux_release_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Game';header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',header,18,62);path.write_bytes(header)
            self.assertEqual(Architecture(path),'x64');entry=Entry(path,'core','0.7.0','x64','https://example.org','linux');self.assertEqual(entry['platform'],'linux')
            header[5]=2;path.write_bytes(header)
            with self.assertRaises(ValueError):Architecture(path)

    def test_linux_payload_rejects_windows_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',header,18,62)
            (root/'BazzaltHub').write_bytes(header);Audit(root,'x64','hub','linux')
            (root/'wrong.dll').write_bytes(b'MZbad')
            with self.assertRaisesRegex(ValueError,'Windows binary'):Audit(root,'x64','hub','linux')

    def test_x86_template_is_permitted_only_in_its_designated_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',header,18,62)
            for name in ('lib/libBazzalt.so','lib/libbazzalt_lua.so','lib/libbazzalt_gui.so','lib/libbazzalt_physics.so','include/Bazzalt/Scene.h'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(header)
            header[4]=1;struct.pack_into('<H',header,18,3);path=root/'RuntimeTemplates/linux-x86/BazzaltPlayer';path.parent.mkdir(parents=True);path.write_bytes(header)
            Audit(root,'x64','core','linux')
            (root/'lib/libBazzalt.so').write_bytes(header)
            with self.assertRaisesRegex(ValueError,'Wrong ELF architecture'):Audit(root,'x64','core','linux')

    @unittest.skipIf(os.name=='nt','POSIX permissions')
    def test_linux_archive_restores_executable_permissions(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);archive=root/'hub.zip'
            with zipfile.ZipFile(archive,'w') as output:
                item=zipfile.ZipInfo('BazzaltHub');item.create_system=3;item.external_attr=(stat.S_IFREG|0o4755)<<16;output.writestr(item,b'ELF')
            Extract(archive,root/'out')
            self.assertEqual(stat.S_IMODE((root/'out/BazzaltHub').stat().st_mode),0o755)

    def test_arm64_crt_excludes_only_the_x64_emulation_shim(self):
        from scripts.stage_game_runtime import RuntimeLibraries
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            def binary(name,machine):
                header=bytearray(256);header[:2]=b'MZ';struct.pack_into('<I',header,60,64);header[64:68]=b'PE\0\0';struct.pack_into('<H',header,68,machine);(root/name).write_bytes(header)
            binary('msvcp140.dll',0xaa64);binary('vcruntime140.dll',0xaa64);binary('vcruntime140_1.dll',0x8664)
            self.assertEqual({file.name for file in RuntimeLibraries(root,'arm64')},{'msvcp140.dll','vcruntime140.dll'})
            binary('unexpected.dll',0x8664)
            with self.assertRaisesRegex(ValueError,'unexpected.dll'):RuntimeLibraries(root,'arm64')
