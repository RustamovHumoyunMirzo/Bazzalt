import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from xml.etree import ElementTree

from Compiler.android_tools import Discover, HostTag, Validate as ValidateTools, NDK_VERSION
from Compiler.android_build import Manifest, Resources, Signing, StageModules, ValidateLibraries
from types import SimpleNamespace
from Compiler.settings import DEFAULTS, Merge, Validate, ApplyPreset


class AndroidTests(unittest.TestCase):
    def Profile(self):
        value=Merge(copy.deepcopy(DEFAULTS))
        value['target']['platform']='android'
        return Validate(value)

    def test_compiler_branding_is_embedded_without_loose_ui_assets(self):
        from scripts.embed_compiler_resources import Generate
        from scripts.compile_resources import Audit
        from Compiler.resources import BrandingPNG
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'Compiler').mkdir()
            assets=root/'bazzalt/assets/branding';assets.mkdir(parents=True)
            image=BrandingPNG()
            (assets/'BazzaltLauncher.png').write_bytes(image)
            generated=Generate(root)
            namespace={};exec(compile(generated.read_text(encoding='utf-8'),str(generated),'exec'),namespace)
            self.assertEqual(namespace['BRANDING_PNG'],image)
            stage=root/'distribution';stage.mkdir()
            (stage/'Compiler').mkdir()
            (stage/'Compiler/_resources.py').write_bytes(generated.read_bytes())
            Audit(stage)
            leaked=stage/'bazzalt/assets/branding';leaked.mkdir(parents=True)
            (leaked/'BazzaltLauncher.png').write_bytes(image)
            with self.assertRaisesRegex(RuntimeError,'Raw application resources'):Audit(stage)

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
                with self.assertRaisesRegex(ValueError,'requires NDK 1.0'):ValidateTools(profile,'arm64',runtime)
                (sdk/'platforms/android-36/android.jar').unlink()
                with self.assertRaisesRegex(ValueError,'missing'):ValidateTools(profile,'x64')

    def test_discovery_prefers_runtime_ndk_revision(self):
        with tempfile.TemporaryDirectory() as folder:
            sdk=Path(folder)/'SDK'
            (sdk/'platforms/android-36').mkdir(parents=True)
            (sdk/'platforms/android-36/android.jar').touch()
            (sdk/'build-tools').mkdir()
            for revision in (NDK_VERSION,'30.0.15000000'):
                ndk=sdk/'ndk'/revision
                (ndk/'toolchains/llvm/prebuilt'/HostTag()).mkdir(parents=True)
                (ndk/'source.properties').write_text('Pkg.Revision = '+revision)
            with patch.dict(os.environ,{'ANDROID_NDK_HOME':str(sdk/'ndk/30.0.15000000'),'ANDROID_NDK_ROOT':''}):
                self.assertEqual(Discover(str(sdk))['ndk_path'],str((sdk/'ndk'/NDK_VERSION).resolve()))

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

    def test_native_library_sdk_and_page_alignment_checks(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'meta').mkdir();libraries=root/'libraries';libraries.mkdir()
            (root/'meta/system_libs.json').write_text(json.dumps({'libc.so':'21','libamidi.so':'29'}))
            header=bytearray(20);header[:4]=b'\x7fELF';header[5]=1;header[16:18]=(3).to_bytes(2,'little')
            (libraries/'libtest.so').write_bytes(header)
            tools=SimpleNamespace(Ndk=root,Compiler=root/'clang++')
            with patch('Compiler.android_build.Run') as run:
                run.return_value.stdout='LOAD 0x0000 0x4000\n (NEEDED) Shared library: [libc.so]'
                ValidateLibraries(libraries,tools,26)
                run.return_value.stdout='LOAD 0x0000 0x4000\n (NEEDED) Shared library: [libamidi.so]'
                with self.assertRaisesRegex(ValueError,'minimum SDK'):ValidateLibraries(libraries,tools,26)
                ValidateLibraries(libraries,tools,29)
                run.return_value.stdout='LOAD 0x0000 0x1000'
                with self.assertRaisesRegex(ValueError,'16 KiB'):ValidateLibraries(libraries,tools,26)


if __name__=='__main__':unittest.main()
