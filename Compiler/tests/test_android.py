import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from xml.etree import ElementTree

from Compiler.android_tools import Discover, HostTag, Validate as ValidateTools, NDK_VERSION
from Compiler.android_build import Manifest, Resources, Signing, StageModules
from Compiler.settings import DEFAULTS, Merge, Validate, ApplyPreset


class AndroidTests(unittest.TestCase):
    def Profile(self):
        value=Merge(copy.deepcopy(DEFAULTS))
        value['target']['platform']='android'
        return Validate(value)

    def test_android_profile_security_and_validation(self):
        value=self.Profile()
        self.assertEqual(value['target']['architecture'],'arm64')
        self.assertEqual(value['security']['key_storage'],'android_keystore')
        for preset in ('low','medium','high','very_high'):
            self.assertEqual(Validate(ApplyPreset(value,preset))['security']['key_storage'],'android_keystore')
        for key,item in (('architecture','x86'),('backend','metal'),('key_storage','file'),('delivery','unknown'),('min_sdk',37)):
            candidate=copy.deepcopy(value)
            candidate['platforms']['android'][key]=item
            with self.subTest(key=key),self.assertRaises(ValueError):Validate(candidate)

    def test_sdk_discovery_and_compatibility(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            sdk=root/'SDK';ndk=root/'NDK';jdk=root/'JDK'
            profile=self.Profile()['platforms']['android']
            profile.update(sdk_path=str(sdk),ndk_path=str(ndk))
            suffix='.exe' if os.name=='nt' else ''
            files=[sdk/'platforms/android-36/android.jar',sdk/'build-tools/36.0.0'/('aapt2'+suffix),
                   sdk/'build-tools/36.0.0'/('zipalign'+suffix),sdk/'build-tools/36.0.0/lib/apksigner.jar',
                   sdk/'build-tools/36.0.0/lib/d8.jar',jdk/('bin/java'+suffix),jdk/('bin/javac'+suffix),
                   ndk/'toolchains/llvm/prebuilt'/HostTag()/('bin/clang++'+suffix)]
            for file in files:file.parent.mkdir(parents=True,exist_ok=True);file.touch()
            (ndk/'source.properties').write_text('Pkg.Revision = '+NDK_VERSION)
            (ndk/'meta').mkdir();(ndk/'meta/platforms.json').write_text(json.dumps(dict(min=21,max=36)))
            with patch.dict(os.environ,{'JAVA_HOME':str(jdk)}),patch('Compiler.android_tools.subprocess.run') as run:
                run.return_value.returncode=0;run.return_value.stderr='openjdk version "17.0.1"';run.return_value.stdout=''
                self.assertEqual(Discover(str(sdk),str(ndk))['ndk_path'],str(ndk.resolve()))
                self.assertEqual(ValidateTools(profile,'arm64').Abi,'arm64-v8a')
                runtime=root/'Runtime';runtime.mkdir()
                (runtime/'runtime.json').write_text(json.dumps(dict(android_ndk='1.0',minimum_api=26)))
                with self.assertRaisesRegex(ValueError,'match the NDK'):ValidateTools(profile,'arm64',runtime)
                (sdk/'platforms/android-36/android.jar').unlink()
                with self.assertRaisesRegex(ValueError,'missing'):ValidateTools(profile,'x64')

    def test_manifests_and_signing_secrets(self):
        value=self.Profile()
        ns='{http://schemas.android.com/apk/res/android}'
        for configuration in ('debug','release'):
            root=ElementTree.fromstring(Manifest(value,configuration))
            self.assertEqual(root.find('application').get(ns+'debuggable'),str(configuration=='debug').lower())
            self.assertEqual(root.find('uses-sdk').get(ns+'minSdkVersion'),'26')
        root=ElementTree.fromstring(Manifest(value,'release',True))
        self.assertEqual(root.get('split'),'config.bazzalt_content')
        self.assertEqual(root.find('application').get(ns+'hasCode'),'false')
        with self.assertRaisesRegex(ValueError,'keystore'):Signing(None,value['platforms']['android'],'release',Path('.'))

    def test_launcher_density_icons(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            Resources(root/'res','',root/'Game.bproject')
            for density,size in (('mdpi',48),('hdpi',72),('xhdpi',96),('xxhdpi',144),('xxxhdpi',192)):
                with Image.open(root/'res'/('mipmap-'+density)/'ic_launcher.png') as image:
                    self.assertEqual(image.size,(size,size))

    def test_native_module_mapping_handles_duplicate_and_unicode_names(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);cooked=root/'Cooked';libraries=root/'libraries';libraries.mkdir()
            header=bytearray(20);header[:4]=b'\x7fELF';header[4]=2;header[5]=1;header[18:20]=(183).to_bytes(2,'little')
            for relative in ('ext/first/main.so','ext/second/main.so','Scripts/Игрок.so'):
                file=cooked/relative;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(header)
            mapping=StageModules(cooked,libraries,'arm64')
            self.assertEqual(len(set(mapping.values())),3)
            self.assertTrue(all(name.startswith('libasset_') and name.endswith('.so') for name in mapping.values()))
            self.assertEqual(mapping,StageModules(cooked,libraries,'arm64'))
            with self.assertRaisesRegex(ValueError,'architecture'):StageModules(cooked,libraries,'x64')


if __name__=='__main__':unittest.main()
