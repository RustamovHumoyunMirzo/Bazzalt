import copy
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from Compiler.settings import Merge,Validate
from Compiler.cross import Resolve,Configure,Triple
from Compiler.verify import Verify
from Compiler.security import SecurityContext
from Compiler.integrity import Seal

class CrossBuildTests(unittest.TestCase):
    def test_cross_toolchain_requires_matching_cpp_abi_and_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);compiler=root/'zig';compiler.touch();settings=Merge({});settings['platforms']['windows'].update(build_method='zig',toolchain=str(compiler));manifest=root/'runtime.json';manifest.write_text('{}')
            with patch('Compiler.cross.HostPlatform',return_value='linux'),patch('Compiler.cross.subprocess.run',return_value=SimpleNamespace(returncode=0,stdout='0.14.1')):
                with self.assertRaisesRegex(ValueError,'matching cross_toolchain'):Resolve(settings,root)
                manifest.write_text(json.dumps(dict(cross_toolchain=dict(method='zig',version='0.14.1',triple='x86_64-windows-gnu'))));options=Resolve(settings,root)
                command=Configure(['ignored','-std=c++20','-shared','source.cpp','-o','output.dll'],options)
                self.assertEqual(command[:4],[str(compiler),'c++','-target','x86_64-windows-gnu']);self.assertNotIn('-fms-runtime-lib=dll',command)
    def test_native_build_does_not_probe_or_require_cross_toolchain(self):
        with patch('Compiler.cross.HostPlatform',return_value='windows'),patch('Compiler.cross.Detect',side_effect=AssertionError):self.assertIsNone(Resolve(Merge({}),Path('unused')))
    def test_offline_verifier_authenticates_encrypted_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);executable=root/'Game';executable.write_bytes(b'\x7fELF'+bytes(128));(root/'game.json').write_text(json.dumps(dict(format_version=3)))
            settings=Merge({});settings['security'].update(encryption='aes-256-gcm',encrypt_manifests=True);security=SecurityContext(settings,b'a'*32);(root/'content.bin').write_bytes(b'asset');Seal(root,executable,security=security)
            self.assertEqual(Verify(root,executable,b'a'*32)['format_version'],3)
            with self.assertRaises(Exception):Verify(root,executable,b'b'*32)
            (root/'content.bin').write_bytes(b'other')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):Verify(root,executable,b'a'*32)
    def test_arm64_binary_detection(self):
        from scripts.release_artifacts import Architecture
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'engine.so';data=bytearray(20);data[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',data,18,183);path.write_bytes(data);self.assertEqual(Architecture(path),'arm64')

    def test_asset_only_foreign_build_uses_shipped_sdk_without_abi_relabeling(self):
        settings=Merge({});settings['platforms']['windows']['build_method']='content_only'
        with patch('Compiler.cross.HostPlatform',return_value='linux'),patch('Compiler.cross.Detect',side_effect=AssertionError):
            self.assertEqual(Resolve(settings,Path('provided-sdk'))['method'],'content_only')

    def test_native_override_is_forwarded_to_script_compiler(self):
        from Compiler.build import Build
        from Compiler.settings import Save
        with tempfile.TemporaryDirectory() as directory:
            project=Path(directory)/'Game.bproject';project.touch();settings=Merge({})
            settings['platforms']['windows']['toolchain']='selected-clang';Save(project,settings)
            with patch('Compiler.cross.Resolve',return_value=None),patch('Compiler.build._Build',return_value='output') as build:
                self.assertEqual(Build(project,'runtime','cooker'),'output')
                self.assertEqual(build.call_args.args[4],'selected-clang')

    def test_automatic_method_honors_a_picked_zig_outside_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);compiler=root/'zig';compiler.touch();settings=Merge({})
            settings['platforms']['windows'].update(build_method='automatic',toolchain=str(compiler))
            (root/'runtime.json').write_text(json.dumps(dict(cross_toolchain=dict(method='zig',version='0.14.1',triple='x86_64-windows-gnu'))))
            with patch('Compiler.cross.HostPlatform',return_value='linux'),patch('Compiler.cross.shutil.which',return_value=None),patch('Compiler.cross.subprocess.run',return_value=SimpleNamespace(returncode=0,stdout='0.14.1')):
                self.assertEqual(Resolve(settings,root)['compiler'],str(compiler))
