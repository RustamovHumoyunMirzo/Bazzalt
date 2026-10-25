import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
import tempfile,unittest,uuid
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.gui.stylesheet_fields import Field,CssString
from Editor.gui.widgets import AssetPickerInput,BoolInput,ColorInput,EnumInput,FloatInput,IntInput,Vec4Input

class StylesheetPropertyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_widgets_use_types_limits_and_css_literals(self):
        locale=LocalizationManager(ResourceManager());runtime=Mock();controller=SimpleNamespace(Window=SimpleNamespace(Localization=locale),Runtime=runtime,SelectedEntity='entity',_updating_inspector=False,_Mutate=lambda _,action:action(),SetDirty=Mock(),_AssetPicker=Mock(return_value=AssetPickerInput()))
        cases=[('float','2.5',FloatInput),('int','2',IntInput),('bool','true',BoolInput),('color','rgba(10,20,30,0.5)',ColorInput),('enum','center',EnumInput),('vec4','1px 2px 3px 4px',Vec4Input),('texture','none',AssetPickerInput)]
        for kind,value,expected in cases:
            with self.subTest(type=kind):
                field=Field(controller,'entity',dict(name='value',type=kind,value=value,options=['start','center','end'],min=0,max=10));self.assertIsInstance(field,expected)
                if kind=='float':field.setValue(7);runtime.SetStylesheetProperty.assert_called_with('entity','value','7')
                if kind=='int':self.assertEqual(field.maximum(),10)
                if kind=='vec4':field.SetValue([4,3,2,1]);self.assertEqual(runtime.SetStylesheetProperty.call_args.args[2],'4px 3px 2px 1px')
                field.deleteLater()
        self.assertEqual(CssString('"Hello"'),'"\\"Hello\\""')
    def test_native_declarations_validation_and_instance_data(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():runtime.Release();self.skipTest('Native runtime is unavailable')
        try:
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory);assets=root/'Assets';assets.mkdir();project=root/'Game.bproject';project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Inputs\nAssetDirectory: Assets\nStartupScene: ""\nProperties: {{}}\n',encoding='utf-8')
                path=assets/'UI.css';path.write_text('@require(accent,red,color,label("Accent"));@require(size,12px,length,min(1),max(80));@require(icon,none,texture);Button{width:require(size);color:require(accent)}',encoding='utf-8')
                self.assertTrue(runtime.LoadProject(project),runtime.LastError());entity=runtime.CreateEntity('Widget');runtime.AddComponent(entity,'GuiElement');self.assertTrue(runtime.SetComponentProperty(entity,'GuiElement','Stylesheet',runtime.AssetInfo(path)['uuid']))
                fields=runtime.StylesheetProperties(entity);self.assertEqual([field['name'] for field in fields],['accent','size','icon']);self.assertEqual(fields[0]['label'],'Accent')
                self.assertTrue(runtime.SetStylesheetProperty(entity,'size','44px'));self.assertFalse(runtime.SetStylesheetProperty(entity,'size','100px'));self.assertFalse(runtime.SetStylesheetProperty(entity,'missing','1'))
                self.assertFalse(runtime.SetStylesheetProperty(entity,'icon',f'asset("{runtime.AssetInfo(path)["uuid"]}")'))
                self.assertEqual(runtime.EntityDetails(entity)['component_data']['GuiElement']['StyleProperties']['size'],'44px')
                self.assertTrue(runtime.ResetStylesheetProperty(entity,'size'));self.assertEqual(runtime.StylesheetProperties(entity)[1]['value'],'12px')
                from Editor.gui.application import Editor
                window=Editor(runtime=runtime)
                try:
                    window.Controller.SelectEntity(entity);section=window.Properties._sections['runtime.GuiElement']
                    self.assertIsInstance(section._fields['Style.icon'],AssetPickerInput)
                    self.assertIsInstance(section._fields['Style.accent'],ColorInput)
                    section._fields['Style.size'].setValue(22)
                    self.assertEqual(runtime.StylesheetProperties(entity)[1]['value'],'22px');self.assertEqual(window.Controller.SelectedEntity,entity)
                    window.Controller.Undo();self.assertEqual(runtime.StylesheetProperties(entity)[1]['value'],'12px')
                finally:window.close();window.deleteLater();self.App.processEvents()
                runtime.Release()
        finally:runtime.Release()
