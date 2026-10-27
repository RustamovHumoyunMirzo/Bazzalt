"""Exercise pinned acquisition against a tiny local Git fixture, never real Jolt."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from scripts import get_jolt

class JoltSourcePreparationTests(unittest.TestCase):
    def test_local_source_pin_dirty_tree_and_wrong_revision_are_enforced(self):
        def git(folder,*args):
            return subprocess.run(["git","-C",str(folder),*args],check=True,capture_output=True,text=True,encoding="utf-8").stdout.strip()
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);upstream=root/"upstream";upstream.mkdir()
            git(upstream,"init");(upstream/"Build").mkdir();(upstream/"Build/CMakeLists.txt").write_text("# fixture only\n",encoding="utf-8")
            git(upstream,"add",".");git(upstream,"-c","user.name=Fixture","-c","user.email=fixture@example.invalid","commit","-m","Fixture")
            revision=git(upstream,"rev-parse","HEAD");git(upstream,"tag","fixture")
            lock=root/"scripts/dependencies/jolt.json";lock.parent.mkdir(parents=True)
            lock.write_text(json.dumps(dict(version="fixture",tag="fixture",commit=revision,repository=str(upstream))),encoding="utf-8")
            with patch.object(get_jolt,"ROOT",root),contextlib.redirect_stdout(io.StringIO()):
                get_jolt.Fetch();get_jolt.Fetch()
                checkout=root/"deps/jolt";self.assertEqual(git(checkout,"rev-parse","HEAD"),revision)
                source=checkout/"Build/CMakeLists.txt";original=source.read_text();source.write_text("# changed\n")
                with self.assertRaisesRegex(RuntimeError,"local changes"):get_jolt.Fetch()
                source.write_text(original);extra=checkout/"untracked.h";extra.write_text("// change\n")
                with self.assertRaisesRegex(RuntimeError,"local changes"):get_jolt.Fetch()
                git(checkout,"add",".");git(checkout,"-c","user.name=Fixture","-c","user.email=fixture@example.invalid","commit","-m","Changed")
                with self.assertRaisesRegex(RuntimeError,"revision integrity"):get_jolt.Fetch()

    def test_existing_incomplete_source_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);lock=root/"scripts/dependencies/jolt.json";lock.parent.mkdir(parents=True)
            lock.write_text(json.dumps(dict(version="fixture",tag="fixture",commit="0"*40,repository="unused")))
            destination=root/"deps/jolt";destination.mkdir(parents=True);sentinel=destination/"keep.txt";sentinel.write_text("keep")
            with patch.object(get_jolt,"ROOT",root),self.assertRaisesRegex(RuntimeError,"Incomplete"):get_jolt.Fetch()
            self.assertEqual(sentinel.read_text(),"keep")
