import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from Compiler.settings import DEFAULTS,Merge,Validate,Effective,Save,Load
from Compiler.macos import Architectures,PlayerHash
from Compiler.security import SecurityContext
from Compiler.build import KeyFor
from bazzalt.platforms import TargetError


def MachO(cpu=0x0100000c,signature=b'signature'):
    header=struct.pack('<8I',0xfeedfacf,cpu,0,2,2,88,0,0)
    segment=bytearray(72);struct.pack_into('<II',segment,0,0x19,72);segment[8:18]=b'__LINKEDIT';struct.pack_into('<Q',segment,32,4096);struct.pack_into('<Q',segment,48,256+len(signature))
    command=struct.pack('<4I',0x1d,16,256,len(signature))
    return header+segment+command+bytes(256-120)+signature

class MacSupportTests(unittest.TestCase):
    def test_independent_platform_profiles_and_migration(self):
        old=copy.deepcopy(DEFAULTS);old['version']=1;old['target']['architecture']='x86';old['window']['backend']='vulkan'
        value=Merge(old);self.assertEqual(value['version'],2);self.assertEqual(value['platforms']['windows']['architecture'],'x86');self.assertEqual(value['platforms']['windows']['backend'],'vulkan')
        value['target']['platform']='macos';value['platforms']['macos'].update(architecture='arm64',backend='metal',key_storage='keychain')
        result=Effective(Validate(value));self.assertEqual(result['target']['architecture'],'arm64');self.assertEqual(result['window']['backend'],'metal');self.assertEqual(result['security']['key_storage'],'keychain')
        self.assertEqual(result['platforms']['windows']['architecture'],'x86')
        result['target']['platform']='windows';self.assertEqual(Effective(result)['target']['architecture'],'x86')
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'Game.bproject';Save(project,result);self.assertEqual(Load(project)['platforms'],result['platforms'])
    def test_invalid_platform_fields(self):
        for key,value in [('architecture','x86'),('backend','vulkan'),('key_storage','dpapi_user'),('bundle_identifier','bad'),('minimum_version','11.0')]:
            settings=Merge({});settings['platforms']['macos'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):Validate(settings)
    def test_native_host_target(self):
        with patch('bazzalt.platforms.HostPlatform',return_value='macos'),patch('bazzalt.platforms.HostArchitecture',return_value='arm64'):
            self.assertEqual(TargetError(dict(platform='macos',architecture='arm64',key_storage='keychain')),'')
            self.assertTrue(TargetError(dict(platform='macos',architecture='x86')))
            self.assertTrue(TargetError(dict(platform='windows',architecture='x64',build_method='native')))
    def test_macho_signing_canonical_hash_and_architecture(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'player';path.write_bytes(MachO());digest=PlayerHash(path);self.assertEqual(Architectures(path),{'arm64'})
            path.write_bytes(MachO(signature=b'different signature much longer'));self.assertEqual(PlayerHash(path),digest)
            data=bytearray(path.read_bytes());data[140]=1;path.write_bytes(data);self.assertNotEqual(PlayerHash(path),digest)
            path.write_bytes(MachO(0x01000007));self.assertEqual(Architectures(path),{'x64'})
            path.write_bytes(bytes.fromhex('cafebabe')+struct.pack('>I',2)+struct.pack('>5I',0x01000007,0,4096,256,0)+struct.pack('>5I',0x0100000c,0,8192,256,0));self.assertEqual(Architectures(path),{'arm64','x64'})
    def test_keychain_never_writes_builder_credentials(self):
        value=Merge({});value['target']['platform']='macos';value['platforms']['macos']['key_storage']='keychain';value['security'].update(encryption='aes-256-gcm',encrypt_manifests=True);value=Effective(value)
        with patch.dict(os.environ,{'BAZZALT_GAME_KEY':'ab'*32}):
            context=SecurityContext(value,KeyFor(value));self.assertEqual(context.FooterKey,bytes(32))
            with tempfile.TemporaryDirectory() as folder:
                boot=json.loads(context.Bootstrap(Path(folder),'0'*64,'1'*64));self.assertEqual(boot['key_storage'],'keychain');self.assertEqual(boot['key_service'],'com.example.game.game');self.assertEqual(list(Path(folder).iterdir()),[]);self.assertNotIn('ab'*32,json.dumps(boot))
        with patch.dict(os.environ,{},clear=True),self.assertRaises(ValueError):KeyFor(value)

    def test_bundle_publication_preserves_presealed_resource_marker(self):
        from Compiler.build import OwnershipMarker,Publish
        from Compiler.macos import CreateBundle
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);staged=root/'Staged.app';CreateBundle(staged,Merge({}))
            marker=OwnershipMarker(staged);marker.write_text('owner',encoding='utf-8');stamp=marker.stat().st_mtime_ns
            self.assertEqual({file.name for file in staged.iterdir()},{'Contents'})
            output=root/'Game.app';Publish(staged,output,'owner')
            self.assertEqual(OwnershipMarker(output).read_text(),'owner');self.assertEqual(OwnershipMarker(output).stat().st_mtime_ns,stamp)
            self.assertFalse((output/'.bazzalt-game-build').exists())

if __name__=='__main__':unittest.main()
