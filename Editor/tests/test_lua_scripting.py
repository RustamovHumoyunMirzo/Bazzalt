import tempfile
import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
from pathlib import Path
from unittest.mock import patch
from Editor.scripting import ScriptCompiler,ScriptAttachments,ScriptValidationError

class LuaScriptingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from PySide6.QtWidgets import QApplication
        cls.App=QApplication.instance() or QApplication([])
    def test_file_changes_import_only_lua_sources_and_defer_during_play(self):
        from unittest.mock import Mock
        from Editor.lua_assets import LuaAssetWatcher
        runtime=Mock();runtime.IsPlaying.return_value=False;runtime.RefreshLuaAssets.return_value=True
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/"Walk.lua";source.write_text("return {}",encoding="utf-8")
            watcher=LuaAssetWatcher(runtime);watcher.Track([source]);finished=[];watcher.Finished.connect(lambda:finished.append(True))
            watcher._Changed(str(source.resolve()));self.assertTrue(watcher.IsBusy())
            runtime.IsPlaying.return_value=True;watcher._Import();runtime.RefreshLuaAssets.assert_not_called()
            runtime.IsPlaying.return_value=False;watcher._Import()
            runtime.RefreshLuaAssets.assert_called_once_with([str(source.resolve())]);self.assertTrue(finished)
            self.assertFalse(watcher.IsBusy());runtime.RefreshAssets.assert_not_called()
            watcher._timer.stop();watcher.deleteLater();self.App.processEvents()
    def test_public_component_fields_have_lua_bindings(self):
        import re
        root=Path(__file__).resolve().parents[2]
        bindings="\n".join(path.read_text(encoding="utf-8") for path in (root/"src/Script").glob("Lua*.cpp"))
        for header in (root/"include/Bazzalt/Components").glob("*.h"):
            text=header.read_text(encoding="utf-8")
            # Data fields are a binding contract; native templates/traits are not.
            for declaration in re.finditer(r"\bstruct\s+(\w+)\s*(?::\s*\w+\s*)?\{",text):
                start=declaration.end();depth=1;end=start
                while depth and end<len(text):
                    depth+=(text[end]=="{")-(text[end]=="}");end+=1
                body=text[start:end-1]
                # Nested DoF settings have their own binding and must not be
                # mistaken for CameraPostProcessing's direct data members.
                body=re.sub(r"struct\s+\w+\s*\{[\s\S]*?\}","",body)
                qualifier={"DepthOfFieldSettings":"DoF"}.get(declaration[1],declaration[1])
                for name in re.findall(r"^\s*(?:float|bool|int|std::uint\d+_t|Vec[234]|Quaternion|UUID|std::string|CameraProjection|CameraAspectMode|LightType|PrimitiveShape|SceneQueryShape)\s+(\w+)\s*(?:=|\{|;)",body,re.M):
                    with self.subTest(header=header.name,type=qualifier,field=name):self.assertIn(f"&{qualifier}::{name}",bindings)
        cmake=(root/"cmake/Lua.cmake").read_text();self.assertIn("add_library(BazzaltLua SHARED",cmake)
        self.assertNotIn("add_library(BazzaltLua STATIC",cmake)
    def test_lua_discovery_attachment_and_no_cpp_compiler(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);assets=root/"Assets";assets.mkdir();source=assets/"Walk.lua"
            source.write_text('-- COMPONENT(Walk)\n-- PROPERTY(float, Speed, 4.0)\nreturn {Speed=4.0}\n',encoding="utf-8")
            compiler=ScriptCompiler(root);descriptor=compiler.Discover()[0]
            self.assertEqual(descriptor.name,"Walk");self.assertEqual(descriptor.properties[0].name,"Speed")
            attachments=ScriptAttachments(root);self.assertTrue(attachments.Attach("entity",descriptor))
            with patch.object(compiler,"_compiler",side_effect=AssertionError("Lua must not invoke the C++ toolchain")):
                result=compiler.Build(attachments.UsedSources());self.assertTrue(result.success);self.assertFalse(result.outputs)
            bindings=attachments.RuntimeBindings(());self.assertEqual(bindings[0]["module"],str(source.resolve()))
            self.assertEqual(attachments.LuaSceneBindings()[0]["properties"]["Speed"],4)
            restored=ScriptAttachments(root/"Restored")
            restored.MergeLuaSceneBindings(attachments.LuaSceneBindings());restored.MergeLuaSceneBindings(attachments.LuaSceneBindings())
            self.assertEqual(len(restored.For("entity")),1)
    def test_duplicate_lua_cpp_types_and_properties_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);assets=root/"Assets";assets.mkdir()
            source=assets/"Walk.lua";source.write_text('-- COMPONENT(Walk)\nreturn {}',encoding="utf-8")
            (assets/"Walk.cpp").write_text('COMPONENT(Walk) {}',encoding="utf-8")
            compiler=ScriptCompiler(root);self.assertEqual(compiler.Discover(),[]);self.assertTrue(compiler.Diagnostics)
            source.write_text('-- PROPERTY(float, Speed, 1)\n-- PROPERTY(float, Speed, 2)\nreturn {}',encoding="utf-8")
            with self.assertRaises(ScriptValidationError):compiler.Inspect(source)
