import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from types import SimpleNamespace
import shutil,tempfile,unittest,uuid
from unittest.mock import Mock,patch
from PySide6.QtWidgets import QApplication
from PySide6.QtTest import QTest
from Editor.runtime import RuntimeService
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.gui.controller import EditorController
from Editor.gui.widgets.font_preview import FontPreview
from Editor.gui.widgets.fields import AssetPickerInput
from Editor.gui_assets import GuiAssetWatcher
from Editor.font_assets import FONT_EXTENSIONS
from Editor.lua_declarations import ReadDeclarations,DeclarationError

class GuiStyleAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_asset_icons_and_stylesheet_creation(self):
        resources=ResourceManager();locale=LocalizationManager(resources)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);panel=AssetBrowserPanel(locale,resources);panel.SetProjectRoot(root)
            with patch.object(resources,'Icon',wraps=resources.Icon) as icon:
                panel._Icon(root/'UI.css');icon.assert_called_with('icons/abrowser/css.svg')
                panel._Icon(root/'UI.ttf');icon.assert_called_with('icons/abrowser/font.svg')
            panel._Create('css');current=Path(panel.Browser.currentItem().data(256));self.assertEqual(current.suffix,'.css');self.assertEqual(current.parent,root);self.assertIn('Button:hover',current.read_text());panel.deleteLater()
    def test_picker_type_filters_and_inspector_routing(self):
        callback=Mock();controller=SimpleNamespace(_AssetPicker=Mock(return_value=callback),_CommitComponent=Mock())
        for component,name,expected in (('Frame','Stylesheet',{'.css'}),('GuiElement','Stylesheet',{'.css'}),('GuiText','Font',FONT_EXTENSIONS),('GuiTextInput','Font',FONT_EXTENSIONS)):
            result=EditorController._ComponentEditor(controller,'entity',component,name,'00000000-0000-0000-0000-000000000000')
            self.assertIs(result,callback);self.assertEqual(controller._AssetPicker.call_args.args[1],expected)
        picker=AssetPickerInput(accepted_extensions=FONT_EXTENSIONS)
        self.assertTrue(picker._Accepts({'extension':'.ttf'}));self.assertFalse(picker._Accepts({'extension':'.css'}));picker.deleteLater()
    def test_font_property_metadata_is_declarative(self):
        declaration=ReadDeclarations('local B=Bazzalt\nlocal S=COMPONENT("Fonts")\nS.Face=PROPERTY("Font", B.Font.new())\nreturn S')
        self.assertEqual(declaration[0],'Fonts');self.assertEqual(declaration[1][0][0],'Font')
        with self.assertRaises(DeclarationError):ReadDeclarations('local S=COMPONENT("Fonts")\nS.Face=PROPERTY("Font", "file.ttf")\nreturn S')
    def test_live_reload_uses_targeted_refresh_and_rewatches_atomic_saves(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'UI.css';path.write_text('Button{}');runtime=SimpleNamespace(GuiAssetSources=lambda:[str(path)],RefreshAsset=Mock(return_value=True),LastError=lambda:'')
            watcher=GuiAssetWatcher(runtime);watcher.Track();watcher.Timer.stop();watcher._Import();runtime.RefreshAsset.assert_called_once_with(str(path.resolve()))
            runtime.RefreshAsset.reset_mock();watcher._Changed(str(path.resolve()));watcher.Timer.stop();watcher._Import();runtime.RefreshAsset.assert_called_once();watcher.deleteLater()
    def test_native_fonts_preview_and_serialized_gui_metadata(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():runtime.Release();self.skipTest('Native runtime is unavailable')
        try:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder)/'Project_Игра';assets=root/'Assets';assets.mkdir(parents=True)
                (root/'Game.bproject').write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: GUI\nAssetDirectory: Assets\nStartupScene: ""\nProperties: {{}}\n',encoding='utf-8')
                css=assets/'UI.css';css.write_text('Button{background-color:red}\n',encoding='utf-8')
                font=next((p for p in (Path('C:/Windows/Fonts/arial.ttf'),Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')) if p.is_file()),None)
                if font:shutil.copy2(font,assets/'Шрифт.ttf')
                self.assertTrue(runtime.LoadProject(root/'Game.bproject'),runtime.LastError())
                self.assertEqual(runtime.AssetInfo(css)['importer'],'Bazzalt.Stylesheet')
                entity=runtime.CreateEntity('Styled');self.assertTrue(runtime.AddComponent(entity,'GuiElement'))
                self.assertTrue(runtime.SetComponentProperty(entity,'GuiElement','Classes','primary wide'));self.assertTrue(runtime.SetComponentProperty(entity,'GuiElement','Attributes','[state="active"]'))
                self.assertFalse(runtime.SetComponentProperty(entity,'GuiElement','Attributes',':hover'))
                if font:
                    path=assets/'Шрифт.ttf';info=runtime.FontInfo(path);self.assertGreater(info['glyphs'],0)
                    image=runtime.FontPreview(path,'AV Привет',32);self.assertEqual(image['height'],192);self.assertTrue(any(image['pixels'][3::4]))
                    preview=FontPreview(path,LocalizationManager(ResourceManager()),runtime);self.assertIsNotNone(preview.Sample.pixmap());preview.Text.setText('New sample');QTest.qWait(140);self.assertIsNotNone(preview.Sample.pixmap());preview.deleteLater()
                    self.assertTrue(runtime.AddComponent(entity,'GuiText'));self.assertTrue(runtime.SetComponentProperty(entity,'GuiText','Font',runtime.AssetInfo(path)['uuid']))
                    self.assertIn(str(path),runtime.GuiAssetSources())
                runtime.Release()
        finally:runtime.Release()

if __name__=='__main__':unittest.main()
