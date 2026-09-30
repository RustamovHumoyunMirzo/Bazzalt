import unittest
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

    def test_compilation_ui_text_is_localized(self):
        localization=LocalizationManager(ResourceManager())
        self.assertEqual(localization.Translate("scripting.compiling"),"Compiling game scripts…")
        self.assertIn("3",localization.Translate("scripting.compile_success",count=3))

if __name__=="__main__":unittest.main()
