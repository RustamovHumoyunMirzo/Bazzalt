import hashlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile
from unittest.mock import patch
from Launcher.downloads import Install,Extract,FetchCatalog

class MacDownloadTests(unittest.TestCase):
    def test_install_native_bundle_and_filter_cpu(self):
        macho=struct.pack('<8I',0xfeedfacf,0x0100000c,0,2,0,0,0,0);prefix='Bazzalt.app/Contents/MacOS/'
        files={'editor.json':json.dumps(dict(version='0.8.0',platform='macos',architecture='arm64',executable=prefix+'Bazzalt')),prefix+'Bazzalt':macho,prefix+'Editor/libBazzalt.dylib':macho,prefix+'Editor/libbshad.dylib':macho,prefix+'Editor/_bazzalt_runtime.so':macho,prefix+'ScriptSDK/lib/libBazzalt.dylib':macho,prefix+'ScriptSDK/include/Bazzalt/Scene.h':'// header'}
        with tempfile.TemporaryDirectory() as folder:
            archive=Path(folder)/'mac.zip'
            with zipfile.ZipFile(archive,'w') as output:
                for name,data in files.items():output.writestr(name,data)
            data=archive.read_bytes();entry=dict(id='editor-0.8.0-macos-arm64',product='editor',platform='macos',architecture='arm64',version='0.8.0',url='https://example.org/mac.zip',size=len(data),sha256=hashlib.sha256(data).hexdigest())
            installed=Install(entry,archive,Path(folder)/'versions');self.assertTrue((installed/prefix/'Bazzalt').is_file())
            from Launcher.catalog import HubCatalog
            with patch('bazzalt.platforms.HostPlatform',return_value='macos'),patch('bazzalt.platforms.HostArchitecture',return_value='arm64'):
                catalog=HubCatalog.__new__(HubCatalog);catalog.Data={'editors':[]};catalog.Save=lambda:None;catalog.DiscoverEditors(Path(folder)/'versions');self.assertEqual(len(catalog.Data['editors']),1)
                self.assertEqual(catalog.Data['editors'][0]['command'],prefix+'Bazzalt')
    def test_bundle_links_cannot_escape_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);archive=root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as output:
                item=zipfile.ZipInfo('Test.app/Contents/escape');item.create_system=3;item.external_attr=(stat.S_IFLNK|0o777)<<16;output.writestr(item,'../../../../outside')
            with self.assertRaises(ValueError):Extract(archive,root/'output',allow_bundle_links=True)
    @unittest.skipIf(os.name=='nt','Creating directory symlinks requires Windows developer mode')
    def test_bundle_framework_links_are_bounded_and_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);archive=root/'good.zip'
            with zipfile.ZipFile(archive,'w') as output:
                output.writestr('Test.app/Contents/Frameworks/Foo/Versions/A/Foo',b'framework')
                item=zipfile.ZipInfo('Test.app/Contents/Frameworks/Foo/Versions/Current');item.create_system=3;item.external_attr=(stat.S_IFLNK|0o777)<<16;output.writestr(item,'A')
            Extract(archive,root/'output',allow_bundle_links=True);self.assertEqual((root/'output/Test.app/Contents/Frameworks/Foo/Versions/Current/Foo').read_bytes(),b'framework')
