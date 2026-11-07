import os, tempfile, unittest, uuid

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService


class NavigationEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_bake_import_component_fields_and_scene_roundtrip(self):
        runtime = RuntimeService()
        if not runtime.IsAvailable() or not hasattr(runtime._host, "bake_navigation"):
            runtime.Release()
            self.skipTest("Updated native runtime required")
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "Assets").mkdir()
                project = root / "Test.bproject"
                project.write_text(
                    f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Navigation\nAssetDirectory: Assets\nStartupScene: ""\nProperties: {{}}\n',
                    encoding="utf-8",
                )
                self.assertTrue(runtime.LoadProject(project), runtime.LastError())
                floor = runtime.CreateEntity("Floor")
                self.assertTrue(runtime.AddComponent(floor, "Primitive Object"))
                self.assertTrue(
                    runtime.SetComponentProperty(floor, "Primitive Object", "Shape", 4)
                )
                self.assertTrue(
                    runtime.SetComponentProperty(floor, "Primitive Object", "Width", 20)
                )
                self.assertTrue(
                    runtime.SetComponentProperty(floor, "Primitive Object", "Depth", 20)
                )
                surface = runtime.CreateEntity("Navigation")
                self.assertTrue(runtime.AddComponent(surface, "NavMesh Surface"))
                self.assertTrue(
                    runtime.SetComponentProperty(
                        surface, "NavMesh Surface", "Size", (20, 10, 20)
                    )
                )
                path = root / "Assets" / "Test.bnavmesh"
                runtime.BakeNavigation(surface, path)
                self.assertTrue(path.is_file())
                self.assertTrue(runtime.RefreshAsset(path))
                asset = runtime.AssetInfo(str(path))
                self.assertEqual(asset["importer"], "Bazzalt.Navigation")
                self.assertFalse(asset.get("error"))
                self.assertTrue(
                    runtime.SetComponentProperty(
                        surface, "NavMesh Surface", "Data", asset["uuid"]
                    )
                )
                agent = runtime.CreateEntity("Agent")
                self.assertTrue(runtime.AddComponent(agent, "Navigation Agent"))
                self.assertTrue(
                    runtime.SetComponentProperty(
                        agent, "Navigation Agent", "Surface", surface
                    )
                )
                self.assertTrue(
                    runtime.SetComponentProperty(agent, "Navigation Agent", "Speed", 7)
                )
                self.assertFalse(
                    runtime.SetComponentProperty(agent, "Navigation Agent", "Speed", -1)
                )
                self.assertTrue(
                    runtime.SetComponentEnabled(agent, "Navigation Agent", False)
                )
                output = root / "Assets" / "Main.bscene"
                self.assertTrue(runtime.SaveScene(output))
                self.assertTrue(runtime.LoadScene(output))
                fields = runtime.EntityDetails(agent)["component_data"][
                    "Navigation Agent"
                ]
                self.assertEqual(fields["Speed"], 7)
                self.assertEqual(fields["Surface"], surface)
                self.assertFalse(
                    runtime.EntityDetails(agent)["component_enabled"][
                        "Navigation Agent"
                    ]
                )
                runtime.Release()
        finally:
            runtime.Release()


if __name__ == "__main__":
    unittest.main()
