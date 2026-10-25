import json
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scripts.collect_third_party import Collect

class ThirdPartyNoticesTests(unittest.TestCase):
    def test_upstream_snapshots_match_recorded_hashes(self):
        root=Path(__file__).resolve().parents[1]/'licenses/third_party'
        records=json.loads((root/'sources.json').read_text(encoding='utf-8'))
        self.assertGreater(len(records),2)
        for record in records:
            path=(root/record['file']).resolve();self.assertTrue(path.is_relative_to(root.resolve()))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),record['sha256'],record['file'])
    def test_collects_native_notices_and_inventory_without_gui(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'source';source=root/'deps/entt/LICENSE';source.parent.mkdir(parents=True);source.write_text('Copyright upstream\nMIT license\n')
            (root/'THIRD_PARTY.md').write_text('# Dependencies\n')
            output=Path(folder)/'licenses';inventory=Collect(output,root)
            self.assertEqual((output/'EnTT.txt').read_text(),source.read_text())
            self.assertEqual(json.loads((output/'inventory.json').read_text()),inventory)
            self.assertTrue((output/'THIRD_PARTY.md').is_file());self.assertFalse((output/'python').exists())
    def test_missing_python_packages_are_optional(self):
        import importlib.metadata
        with tempfile.TemporaryDirectory() as folder,patch('scripts.collect_third_party.importlib.metadata.distribution',side_effect=importlib.metadata.PackageNotFoundError):
            output=Path(folder)/'licenses';Collect(output,Path(folder),python=True)
            self.assertTrue((output/'inventory.json').exists())
