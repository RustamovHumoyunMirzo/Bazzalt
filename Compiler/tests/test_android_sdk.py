"""Real Java/dex/signing integration. Android CI enables this with its installed SDK."""
import copy
import os
from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile

from Compiler.android_tools import Validate
from Compiler.android_build import Package, Signing, ValidateLibraries
from Compiler.build import Run
from Compiler.settings import Merge, DEFAULTS


@unittest.skipUnless(os.environ.get('BAZZALT_ANDROID_SDK_TESTS')=='1','Android SDK integration is enabled on Android CI')
class AndroidSdkTests(unittest.TestCase):
    def test_real_java_dex_signing_and_delivery(self):
        repository=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            settings=Merge(copy.deepcopy(DEFAULTS))
            settings['target']['platform']='android'
            settings['_project']=str(root/'Game.bproject')
            profile=settings['platforms']['android']
            tools=Validate(profile,'arm64')
            runtime=root/'runtime'
            shutil.copytree(repository/'deps/SDL3/android-project/app/src/main/java',runtime/'android/java')
            payload=root/'payload';payload.mkdir();(payload/'game.json').write_text('{}')
            libraries=root/'libraries';libraries.mkdir()
            probe=root/'probe.cpp';probe.write_text('extern "C" int BazzaltAndroidProbe(){return 1;}')
            Run([tools.Compiler,'--target='+tools.Triple+'26','--sysroot='+str(tools.Sysroot),'-shared','-fPIC',
                 '-nostdlib++','-Wl,-z,max-page-size=16384',probe,'-o',libraries/'libprobe.so'])
            ValidateLibraries(libraries,tools,26)
            for mode in ('apk','split_apk','obb','external_data'):
                with self.subTest(mode=mode):
                    profile['delivery']=mode
                    work=root/mode;work.mkdir();output=work/'output';output.mkdir()
                    apks,delivery=Package(tools,settings,'debug',work,output,payload,libraries,runtime,
                                          Signing(tools,profile,'debug',root),lambda:None)
                    self.assertEqual(len(apks),2 if mode=='split_apk' else 1)
                    with zipfile.ZipFile(apks[0]) as base:
                        self.assertIn('classes.dex',base.namelist())
                        self.assertIn('assets/delivery.json',base.namelist())
                        self.assertEqual('assets/bazzalt/game.json' in base.namelist(),mode=='apk')
                    if mode in ('obb','external_data'):
                        with zipfile.ZipFile(output/delivery['archive']) as archive:
                            self.assertEqual(archive.read('game.json'),b'{}')


if __name__=='__main__':unittest.main()
