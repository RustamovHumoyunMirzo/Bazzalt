import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import tempfile
import uuid
import unittest
from pathlib import Path
from PySide6.QtCore import QEvent
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor
from Editor.gui.widgets import AssetPickerInput

class TextureAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Temp=tempfile.TemporaryDirectory();self.Root=Path(self.Temp.name);(self.Root/"Assets").mkdir()
        project=self.Root/"Test.bproject";project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Textures\nAssetDirectory: Assets\nStartupScene: ""\nProperties:\n',encoding="utf-8")
        self.Window=Editor();self.Window.Controller.Timer.stop()
        self.assertTrue(self.Window.Runtime.LoadProject(project));self.Window.AssetBrowser.SetProjectRoot(self.Root/"Assets")
    def tearDown(self):
        self.Window.Controller.Timer.stop();self.Window.Controller.SetDirty(False);self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete);self.Temp.cleanup()
    def test_create_texture_and_asset_properties_are_not_components(self):
        w=self.Window;w.AssetBrowser._Create("texture");path=self.Root/"Assets"/"New Texture.btexture"
        self.assertTrue(path.exists());self.assertEqual(w.AssetBrowser.CurrentFolder(),self.Root/"Assets")
        self.assertEqual(Path(w.AssetBrowser.Browser.currentItem().data(256)),path)
        self.assertTrue(w.Runtime.RefreshAssets());w.Controller.SelectAsset(path)
        self.assertTrue(w.Properties.AddComponentButton.isHidden());section=w.Properties._sections["asset.texture"]
        self.assertEqual(section._fields["Width (pixels)"].value(),512)
        section._fields["Width (pixels)"].setValue(128)
        self.assertEqual(w.Runtime.TextureAssetInfo(path)["Width"],128)
        self.assertIn("Width: 128",path.read_text());self.assertFalse(w.Controller.SelectedEntity)
    def test_camera_and_image_pick_same_texture_and_reject_wrong_types(self):
        w=self.Window;w.AssetBrowser._Create("texture");path=self.Root/"Assets"/"New Texture.btexture";self.assertTrue(w.Runtime.RefreshAssets());identity=w.Runtime.AssetInfo(path)["uuid"]
        camera=w.Runtime.CreateEntity("Camera");w.Runtime.AddComponent(camera,"Camera");w.Controller.SelectEntity(camera,force=True)
        picker=w.Properties._sections["runtime.Camera"]._fields["Render Target"]
        self.assertIsInstance(picker,AssetPickerInput);self.assertEqual(picker._accepted_extensions,{".btexture"});picker.SetValue(identity)
        self.assertEqual(w.Runtime.EntityDetails(camera)["component_data"]["Camera"]["Render Target"],identity)
        self.assertFalse(picker._Accepts({"extension":".png"}));self.assertTrue(picker._Accepts({"extension":".btexture"}))
        image=w.Runtime.CreateEntity("Image");w.Runtime.AddComponent(image,"GuiImage");w.Controller.SelectEntity(image,force=True)
        field=w.Properties._sections["runtime.GuiImage"]._fields["Texture"];self.assertTrue(field._Accepts({"extension":".btexture"}));field.SetValue(identity)
        self.assertEqual(w.Runtime.EntityDetails(image)["component_data"]["GuiImage"]["Texture"],identity)
        self.assertFalse(w.Runtime.SaveTextureAsset(self.Root.parent/"outside.btexture",w.Runtime.TextureAssetInfo(path)))
        self.assertFalse((self.Root.parent/"outside.btexture").exists())
