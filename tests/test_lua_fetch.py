import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from scripts import get_lua

class LuaFetchTests(unittest.TestCase):
    def archive(self, github=True):
        buffer=io.BytesIO()
        with tarfile.open(fileobj=buffer,mode='w:gz') as archive:
            names=[name+'.c' for name in get_lua.RUNTIME_SOURCES]+['lua.h','luaconf.h','lauxlib.h','lualib.h','../../escape.c']
            if not github:names+=['lua.hpp']
            for name in names:
                data=b'/* Copyright (C) fixture\nPermission is hereby granted, free of charge. */' if name=='lua.h' else b'/* fixture */';member=tarfile.TarInfo(('lua-pin/' if github else 'lua-release/src/')+name);member.size=len(data);archive.addfile(member,io.BytesIO(data))
            if not github:
                for name in ('README','doc/readme.html','Makefile'):
                    data=b'official fixture';member=tarfile.TarInfo('lua-release/'+name);member.size=len(data);archive.addfile(member,io.BytesIO(data))
        return buffer.getvalue()
    def test_verified_fallback_is_complete_and_idempotent(self):
        data=self.archive()
        with tempfile.TemporaryDirectory() as folder:
            destination=Path(folder)/'deps/lua'
            with patch.object(get_lua,'GITHUB_HASH',hashlib.sha256(data).hexdigest()),patch.object(get_lua.urllib.request,'urlopen',side_effect=[OSError('unavailable'),io.BytesIO(data)]) as fetch:
                get_lua.Install(destination)
                self.assertEqual(fetch.call_count,2)
                self.assertTrue((destination/'src/lua.hpp').is_file())
                self.assertIn('Permission is hereby granted',(destination/'README').read_text())
                from scripts.collect_third_party import Collect
                output=Path(folder)/'licenses';Collect(output,Path(folder))
                self.assertIn('Permission is hereby granted',(output/'Lua.txt').read_text())
                self.assertEqual((destination/'.bazzalt-source-sha256').read_text().strip(),get_lua.RELEASE_HASH)
                self.assertFalse((Path(folder)/'escape.c').exists())
                (destination/'README').unlink()
                get_lua.Install(destination)
                self.assertIn('Permission is hereby granted',(destination/'README').read_text())
                self.assertEqual(fetch.call_count,2)
    def test_primary_release_keeps_packaging_and_documentation_layout(self):
        data=self.archive(False)
        with tempfile.TemporaryDirectory() as folder:
            destination=Path(folder)/'lua'
            with patch.object(get_lua,'RELEASE_HASH',hashlib.sha256(data).hexdigest()),patch.object(get_lua.urllib.request,'urlopen',return_value=io.BytesIO(data)):
                get_lua.Install(destination)
            self.assertTrue((destination/'doc/readme.html').is_file())
            self.assertTrue((destination/'Makefile').is_file())
            self.assertIn('Permission is hereby granted',(destination/'README').read_text())
    def test_bad_checksum_publishes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            destination=Path(folder)/'deps/lua'
            with patch.object(get_lua.urllib.request,'urlopen',return_value=io.BytesIO(b'corrupt')):
                with self.assertRaises(ValueError):get_lua.Install(destination,True)
            self.assertFalse(destination.exists())
