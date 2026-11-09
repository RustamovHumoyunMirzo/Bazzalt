import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from scripts import get_lua

class LuaFetchTests(unittest.TestCase):
    def archive(self):
        buffer=io.BytesIO()
        with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
            names=[name+'.c' for name in get_lua.RUNTIME_SOURCES]+['lua.h','luaconf.h','lauxlib.h','lualib.h','../../escape.c']
            for name in names:
                data=b'/* fixture */';member=tarfile.TarInfo('lua-pin/'+name);member.size=len(data);archive.addfile(member,io.BytesIO(data))
        return buffer.getvalue()
    def test_verified_fallback_is_complete_and_idempotent(self):
        data=self.archive()
        with tempfile.TemporaryDirectory() as folder:
            destination=Path(folder)/'lua'
            with patch.object(get_lua,'GITHUB_HASH',hashlib.sha256(data).hexdigest()),patch.object(get_lua.urllib.request,'urlopen',side_effect=[OSError('unavailable'),io.BytesIO(data)]) as fetch:
                get_lua.Install(destination)
                self.assertEqual(fetch.call_count,2)
                self.assertTrue((destination/'src/lua.hpp').is_file())
                self.assertEqual((destination/'.bazzalt-source-sha256').read_text().strip(),get_lua.RELEASE_HASH)
                self.assertFalse((Path(folder)/'escape.c').exists())
                get_lua.Install(destination)
                self.assertEqual(fetch.call_count,2)
    def test_bad_checksum_publishes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            destination=Path(folder)/'lua'
            with patch.object(get_lua.urllib.request,'urlopen',return_value=io.BytesIO(b'corrupt')):
                with self.assertRaises(ValueError):get_lua.Install(destination,True)
            self.assertFalse(destination.exists())
