import unittest
import tempfile
import json
from pathlib import Path
from Editor.scripting import ScriptCompiler,ScriptAttachments,ScriptValidationError,ScriptDescriptor,ScriptProperty


class ScriptValidationTests(unittest.TestCase):
    def setUp(self):
        self.Temp=tempfile.TemporaryDirectory();self.Root=Path(self.Temp.name);self.Assets=self.Root/"Assets";self.Assets.mkdir();self.Compiler=ScriptCompiler(self.Root)
    def tearDown(self):self.Temp.cleanup()
    def Write(self,name,source):
        path=self.Assets/name;path.write_text(source,encoding="utf-8");return path
    def test_duplicate_types_are_not_offered_or_built_until_renamed(self):
        first=self.Write("code0.cpp","COMPONENT(MyComp) { PROPERTY(float, speed, 1.0f) };")
        second=self.Write("code1.cpp",first.read_text())
        self.assertEqual(self.Compiler.Discover(),[])
        message=self.Compiler.Diagnostics[0].message
        self.assertIn("MyComp",message);self.assertIn("code0.cpp",message);self.assertIn("code1.cpp",message)
        self.assertFalse(self.Compiler.Build([first]).success)
        with self.assertRaises(ScriptValidationError):self.Compiler.ValidateDescriptor(self.Compiler.Inspect(first))
        second.write_text("COMPONENT(OtherComp) { PROPERTY(float, speed, 2.0f) };",encoding="utf-8")
        self.assertEqual({value.name for value in self.Compiler.Discover()},{"MyComp","OtherComp"})
        self.Compiler.ValidateDescriptor(self.Compiler.Inspect(first))
    def test_attachment_rejects_same_type_from_another_source(self):
        first=self.Write("code0.cpp","COMPONENT(MyComp) { PROPERTY(float, speed, 1.0f) };")
        second=self.Write("code1.cpp",first.read_text());store=ScriptAttachments(self.Root)
        self.assertTrue(store.Attach("entity",self.Compiler.Inspect(first)))
        self.assertFalse(store.Attach("entity",self.Compiler.Inspect(second)))
        self.assertEqual(len(store.For("entity")),1);self.assertEqual(store.For("entity")[0]["properties"],{"speed":1.0})
    def test_duplicate_fields_and_multiple_component_bodies_are_rejected(self):
        for source in ("COMPONENT(Test) { PROPERTY(float, speed, 1) PROPERTY(float, speed, 2) };", "COMPONENT(One) { PROPERTY(float, a, 1) }; COMPONENT(Two) { PROPERTY(float, a, 2) };"):
            path=self.Write("invalid.cpp",source)
            with self.assertRaises(ScriptValidationError):self.Compiler.Inspect(path)
            self.assertEqual(self.Compiler.Discover(),[]);self.assertTrue(self.Compiler.Diagnostics)
    def test_comments_strings_and_fields_outside_component_are_ignored(self):
        path=self.Write("test.cpp",'''// COMPONENT(Fake) { PROPERTY(float, speed, 2) }
const char* note = "COMPONENT(Fake) PROPERTY(float, speed, 3)";
struct Other { PROPERTY(float, outside, 1) };
COMPONENT(Test) {
/* PROPERTY(float, speed, 2) */
PROPERTY(float, speed, 1.0f)
PROPERTY(std::string, title, "hello")
};''')
        descriptor=self.Compiler.Inspect(path)
        self.assertEqual(descriptor.name,"Test");self.assertEqual([p.name for p in descriptor.properties],["speed","title"])
        self.assertEqual(descriptor.properties[1].default,'"hello"')
    def test_legacy_duplicate_manifest_never_merges_inspector_fields_or_runs_both(self):
        store=ScriptAttachments(self.Root);store.values={"entities":{"entity":[
            {"source":str(self.Assets/"one.cpp"),"type":"Test","properties":{"speed":1}},
            {"source":str(self.Assets/"two.cpp"),"type":"Test","properties":{"speed":2}}]}}
        store.Save();loaded=ScriptAttachments(self.Root)
        self.assertEqual(len(loaded.For("entity")),1);self.assertEqual(loaded.For("entity")[0]["properties"],{"speed":1})
        with self.assertRaises(ScriptValidationError):loaded.RuntimeBindings(())
        self.assertTrue(loaded.Remove("entity","Test"));self.assertEqual(loaded.For("entity"),[])
    def test_attach_cannot_bypass_duplicate_property_validation(self):
        descriptor=ScriptDescriptor(self.Assets/"invalid.cpp","Test",(ScriptProperty("float","speed","1"),ScriptProperty("float","speed","2")))
        with self.assertRaises(ScriptValidationError):ScriptAttachments(self.Root).Attach("entity",descriptor)

if __name__=="__main__":unittest.main()
