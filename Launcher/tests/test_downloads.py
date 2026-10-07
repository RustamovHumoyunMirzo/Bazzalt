from __future__ import annotations
import hashlib
import io
import json
from pathlib import Path
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from Launcher.downloads import Download, Extract, FetchCatalog, Install, ValidateEntry
from scripts.release_artifacts import Architecture, Audit, Entry
from bazzalt.tools import FindCompiler
from Editor.scripting import ScriptCompiler


def PE(machine=0x8664):
    data=bytearray(128);data[:2]=b"MZ";struct.pack_into("<I",data,0x3c,64);data[64:68]=b"PE\0\0";struct.pack_into("<H",data,68,machine);return bytes(data)


class Response(io.BytesIO):
    def geturl(self):return "https://example.org/package.zip"


class DownloadTests(unittest.TestCase):
    def setUp(self):
        base=Path(__file__).resolve().parents[2]/"build";base.mkdir(exist_ok=True)
        self.Temporary=tempfile.TemporaryDirectory(dir=base);self.addCleanup(self.Temporary.cleanup);self.Root=Path(self.Temporary.name)

    def MakeEntry(self,data=b"zip",product="editor"):
        return {"id":product+"-0.5.0-windows-x64","product":product,"platform":"windows","architecture":"x64","version":"0.5.0","url":"https://example.org/package.zip","size":len(data),"sha256":hashlib.sha256(data).hexdigest()}

    def Zip(self,files):
        path=self.Root/"package.zip"
        with zipfile.ZipFile(path,"w") as archive:
            for name,data in files.items():archive.writestr(name,data)
        return path

    def EditorZip(self,machine=0x8664):
        return self.Zip({"editor.json":json.dumps({"version":"0.5.0","architecture":"x64","executable":"Bazzalt.exe"}),"Bazzalt.exe":PE(machine),"Editor/Bazzalt.dll":PE(machine),"Editor/bshad.dll":PE(machine),"Editor/_bazzalt_runtime.pyd":PE(machine),"ScriptSDK/include/Bazzalt/Scene.h":"// header","ScriptSDK/lib/Bazzalt.lib":b"lib"})

    def test_download_verifies_digest_size_and_reports_progress(self):
        data=b"payload";progress=[];entry=self.MakeEntry(data)
        with patch("Launcher.downloads.urlopen",return_value=Response(data)):
            Download(entry,self.Root/"download.zip",lambda done,total:progress.append((done,total)))
        self.assertEqual((self.Root/"download.zip").read_bytes(),data);self.assertEqual(progress,[(7,7)])

    def test_download_rejects_bad_hash_and_excess_size(self):
        for name,data in (("hash",b"different"),("size",b"longer payload")):
            with self.subTest(name=name),patch("Launcher.downloads.urlopen",return_value=Response(data)),self.assertRaises(ValueError):
                Download(self.MakeEntry(b"123456789"),self.Root/name)

    def test_download_cancellation(self):
        with patch("Launcher.downloads.urlopen",return_value=Response(b"data")),self.assertRaises(InterruptedError):
            Download(self.MakeEntry(b"data"),self.Root/"cancel",cancelled=lambda:True)

    def test_catalog_validation(self):
        for change in ({"url":"http://example.org"},{"sha256":"bad"},{"id":"../escape"},{"size":True},{"version":"bad"}):
            with self.subTest(change=change),self.assertRaises((ValueError,TypeError)):ValidateEntry(self.MakeEntry()|change)
        value={"schema_version":1,"downloads":[self.MakeEntry()|{"architecture":"x64" if struct.calcsize("P")==8 else "x86"}]}
        with patch("Launcher.downloads.urlopen",return_value=Response(json.dumps(value).encode())):
            self.assertEqual(len(FetchCatalog("https://example.org/catalog")),1)

    def test_catalog_keeps_x86_game_sdk_but_excludes_x86_editor(self):
        entries=[self.MakeEntry(),self.MakeEntry()|{"architecture":"x86"},self.MakeEntry(product="core")|{"architecture":"x86"}]
        with patch("Launcher.downloads.urlopen",return_value=Response(json.dumps({"schema_version":1,"downloads":entries}).encode())):
            result=FetchCatalog("https://example.org/catalog")
        self.assertEqual([(item["product"],item["architecture"]) for item in result],[("editor","x64"),("core","x86")])

    def test_x86_editor_is_rejected_before_download_or_install(self):
        entry=self.MakeEntry()|{"architecture":"x86"}
        with patch("Launcher.downloads.urlopen") as request:
            with self.assertRaisesRegex(ValueError,"64-bit"):Download(entry,self.Root/"download.zip")
            request.assert_not_called()
        with self.assertRaisesRegex(ValueError,"64-bit"):Install(entry,self.Root/"missing.zip",self.Root/"versions")

    def test_zip_rejects_traversal_windows_devices_and_ads(self):
        for index,name in enumerate(("../escape","C:/escape","/escape","folder/NUL.txt","folder/file:stream","folder/file. ","..\\escape")):
            with self.subTest(name=name):
                archive=self.Zip({name:"bad"});target=self.Root/f"extract{index}";target.mkdir()
                with self.assertRaises(ValueError):Extract(archive,target)
        self.assertFalse((self.Root.parent/"escape").exists())

    def test_zip_rejects_links_and_case_collisions(self):
        archive=self.Root/"links.zip"
        with zipfile.ZipFile(archive,"w") as output:
            entry=zipfile.ZipInfo("link");entry.external_attr=(stat.S_IFLNK|0o777)<<16;output.writestr(entry,"outside")
        with self.assertRaises(ValueError):Extract(archive,self.Root/"extract")
        archive=self.Zip({"File.txt":"a","file.txt":"b"})
        with self.assertRaises(ValueError):Extract(archive,self.Root/"extract2")

    def test_install_publishes_complete_editor_once(self):
        archive=self.EditorZip();entry=self.MakeEntry(archive.read_bytes());versions=self.Root/"versions"
        target=Install(entry,archive,versions)
        self.assertEqual(target,versions/"bazzalt_0_5_0");self.assertTrue((target/"Bazzalt.exe").is_file())
        with self.assertRaises(ValueError):Install(entry,archive,versions)
        self.assertEqual(list(versions.iterdir()),[target])

    def test_install_rejects_wrong_architecture_without_partial_version(self):
        archive=self.EditorZip(0x14c);versions=self.Root/"versions"
        with self.assertRaises(ValueError):Install(self.MakeEntry(archive.read_bytes()),archive,versions)
        self.assertEqual(list(versions.iterdir()),[])

    def test_install_shared_llvm_not_per_editor(self):
        archive=self.Zip({"bin/clang++.exe":PE()});tools=self.Root/"shared-tools"
        with patch("Launcher.downloads.DataPaths.Tools",return_value=tools):
            target=Install(self.MakeEntry(archive.read_bytes(),"llvm"),archive,self.Root/"versions")
        self.assertEqual(target,tools/"llvm");self.assertFalse((self.Root/"versions").exists())

    def test_gui_module_is_required_only_for_declared_editor_packages(self):
        archive=self.EditorZip()
        with zipfile.ZipFile(archive) as source:files={name:source.read(name) for name in source.namelist()}
        files["editor.json"]=json.dumps({"version":"0.5.0","architecture":"x64","executable":"Bazzalt.exe","gui_abi":1})
        archive=self.Zip(files)
        with self.assertRaisesRegex(ValueError,"Missing GUI runtime library"):
            Install(self.MakeEntry(archive.read_bytes()),archive,self.Root/"versions")
        files.update({"Editor/bazzalt_gui.dll":PE(),"ScriptSDK/lib/bazzalt_gui.dll":PE()})
        archive=self.Zip(files)
        target=Install(self.MakeEntry(archive.read_bytes()),archive,self.Root/"versions")
        self.assertTrue((target/"Editor/bazzalt_gui.dll").is_file())

    def test_core_audit_requires_gui_runtime(self):
        (self.Root/"include/Bazzalt").mkdir(parents=True);(self.Root/"include/Bazzalt/Scene.h").write_text("// header")
        (self.Root/"lib").mkdir();(self.Root/"lib/Bazzalt.dll").write_bytes(PE());(self.Root/"lib/bazzalt_lua.dll").write_bytes(PE())
        with self.assertRaisesRegex(ValueError,"bazzalt_gui.dll"):Audit(self.Root,"x64","core")
        (self.Root/"lib/bazzalt_gui.dll").write_bytes(PE());Audit(self.Root,"x64","core")

    def test_release_architecture_and_hub_separation(self):
        executable=self.Root/"BazzaltHub.exe";executable.write_bytes(PE());self.assertEqual(Architecture(executable),"x64");Audit(self.Root,"x64","hub")
        with self.assertRaises(ValueError):Audit(self.Root,"x86","hub")
        (self.Root/"versions").mkdir()
        with self.assertRaises(ValueError):Audit(self.Root,"x64","hub")
        entry=Entry(executable,"hub","1.0.0","x64","https://example.org/releases");ValidateEntry(entry)

    def test_compiler_resolution_shared_override_and_invalid_override(self):
        tools=self.Root/"tools";compiler=tools/"llvm/bin/clang++.exe";compiler.parent.mkdir(parents=True);compiler.write_bytes(PE())
        with patch("bazzalt.tools.DataPaths.Tools",return_value=tools):
            self.assertEqual(FindCompiler(),compiler);self.assertEqual(FindCompiler(str(compiler)),compiler);self.assertIsNone(FindCompiler(str(self.Root/"missing")))

    def test_missing_and_unlaunchable_compilers_return_diagnostics(self):
        source=self.Root/"Assets/Walk.cpp";source.parent.mkdir();source.write_text("COMPONENT(Walk) { PROPERTY(float, Speed, 1.0f); };",encoding="utf-8")
        compiler=ScriptCompiler(self.Root,engine_root=self.Root,app_root=self.Root,tools={"compiler_path":str(self.Root/"missing")})
        result=compiler.Build([source]);self.assertFalse(result.success);self.assertEqual(result.diagnostics[0].localization_key,"scripting.toolchain_missing")
        sdk=self.Root/"ScriptSDK";headers=sdk/"include/Bazzalt";headers.mkdir(parents=True);(headers/"Scene.h").write_text("// header");entt=sdk/"include/entt/entity";entt.mkdir(parents=True);(entt/"registry.hpp").write_text("// entt");(sdk/"lib").mkdir();(sdk/"lib/Bazzalt.lib").write_bytes(b"lib")
        fake=self.Root/"clang++.exe";fake.write_bytes(PE());compiler.Tools={"compiler_path":str(fake)}
        with patch("Editor.scripting.subprocess.run",side_effect=OSError("Cannot launch compiler")):
            result=compiler.Build([source])
        self.assertFalse(result.success);self.assertIn("Cannot launch compiler",result.diagnostics[0].message)


if __name__=="__main__":unittest.main()
