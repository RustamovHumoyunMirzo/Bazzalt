import unittest
import tempfile
from pathlib import Path

from Editor.scripting import ScriptAttachments, ScriptCompiler
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager

class ScriptingTests(unittest.TestCase):
    source=Path(__file__).parent/"fixtures"/"scripts"/"Mover.cpp"
    def test_inspects_component_and_properties(self):
        value=ScriptCompiler.Inspect(self.source)
        self.assertIsNotNone(value);self.assertEqual(value.name,"Mover")
        self.assertEqual([p.name for p in value.properties],["Speed","Active"])

    def test_attachment_is_unique_and_persistent(self):
        root=Path(__file__).parent/"fixtures"/"script-project";manifest=root/".bazzalt"/"ScriptAttachments.json"
        if manifest.exists():manifest.unlink()
        descriptor=ScriptCompiler.Inspect(self.source);store=ScriptAttachments(root)
        try:
            self.assertTrue(store.Attach("entity",descriptor));self.assertFalse(store.Attach("entity",descriptor))
            loaded=ScriptAttachments(root);self.assertEqual(loaded.For("entity")[0]["properties"]["Speed"],2.5)
        finally:
            if manifest.exists():manifest.unlink()

    def test_generated_module_has_versioned_entry_point(self):
        descriptor=ScriptCompiler.Inspect(self.source);wrapper=ScriptCompiler._Wrapper(descriptor)
        self.assertIn("BazzaltGetScriptModuleV1",wrapper)
        self.assertIn('strcmp(name,"Speed")',wrapper)
        self.assertIn("OnUpdate(void* p,float dt)",wrapper)
        self.assertIn("BazzaltBindTimeV1",wrapper)
        self.assertIn("BazzaltBindInputV1",wrapper)
        self.assertIn("BazzaltFixedUpdateV1",wrapper)

    def test_compilation_ui_text_is_localized(self):
        localization=LocalizationManager(ResourceManager())
        self.assertEqual(localization.Translate("scripting.compiling"),"Compiling game scripts…")
        self.assertIn("3",localization.Translate("scripting.compile_success",count=3))

    def test_time_behavior_compiles_with_bundled_toolchain(self):
        root=Path(__file__).resolve().parents[2]
        if ScriptCompiler(root,engine_root=root)._compiler() is None:self.skipTest("bundled toolchain is unavailable")
        with tempfile.TemporaryDirectory(prefix="time-script-test-",dir=root/"build") as project:
            compiler=ScriptCompiler(project,engine_root=root)
            result=compiler.Build([self.source.with_name("TimeProbe.cpp")])
            self.assertTrue(result.success,"\n".join(item.message for item in result.diagnostics))
            self.assertTrue(result.outputs[0].is_file())
            self.assertEqual(compiler.Build([self.source.with_name("TimeProbe.cpp")]).outputs,result.outputs)

    def test_material_behavior_has_typed_uuid_setters_and_service_binding(self):
        descriptor=ScriptCompiler.Inspect(self.source.with_name("MaterialProbe.cpp"));wrapper=ScriptCompiler._Wrapper(descriptor)
        self.assertIn("Bazzalt::Material::Load(id)",wrapper);self.assertIn("Bazzalt::Shader::Load(id)",wrapper);self.assertIn("BazzaltBindMaterialsV1",wrapper)

if __name__=="__main__":unittest.main()
