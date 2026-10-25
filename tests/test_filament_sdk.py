import io,tarfile,tempfile,unittest
from pathlib import Path
from scripts.extract_filament_sdk import Extract,Normalize

class FilamentSdkTests(unittest.TestCase):
    def test_flat_and_wrapped_official_layouts(self):
        for prefix in ('','filament/'):
            with self.subTest(prefix=prefix),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);archive=root/'sdk.tgz'
                with tarfile.open(archive,'w:gz') as output:
                    for name,data in (('include/filament/Engine.h',b'header'),('bin/matc',b'tool'),('lib/x86_64/libfilament.a',b'library')):
                        item=tarfile.TarInfo(prefix+name);item.size=len(data);item.mode=0o755 if name.startswith('bin/') else 0o644;output.addfile(item,io.BytesIO(data))
                Extract(archive,root/'sdk');self.assertEqual((root/'sdk/include/filament/Engine.h').read_bytes(),b'header');self.assertTrue(Normalize(root/'sdk'))

    def test_recover_existing_wrapped_download(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);header=root/'filament/include/filament/Engine.h';header.parent.mkdir(parents=True);header.write_text('header')
            self.assertTrue(Normalize(root));self.assertTrue((root/'include/filament/Engine.h').is_file());self.assertFalse((root/'filament').exists())

    def test_invalid_and_escaping_archives_fail_without_publishing_sdk(self):
        for name in ('wrong/header.h','../outside'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);archive=root/'sdk.tgz'
                with tarfile.open(archive,'w:gz') as output:
                    item=tarfile.TarInfo(name);item.size=1;output.addfile(item,io.BytesIO(b'x'))
                with self.assertRaises((ValueError,tarfile.FilterError)):Extract(archive,root/'sdk')
                self.assertEqual(list((root/'sdk').iterdir()),[]);self.assertFalse((root/'outside').exists())
