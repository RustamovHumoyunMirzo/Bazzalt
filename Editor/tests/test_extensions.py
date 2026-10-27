import os,tempfile,unittest,json
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from unittest.mock import patch
import yaml
from PySide6.QtCore import QEvent,Qt
from PySide6.QtWidgets import QApplication,QPushButton,QLabel,QSpinBox
from Editor.extensions import ValidateUI
from bazzalt.extensions import ExtensionError,BuildPackage,ExtensionStore
from Compiler.extensions import BuildExtensionPackage
ROOT=Path(__file__).resolve().parents[2]
UI='''<ui version="4.0"><class>Tool</class><widget class="QWidget" name="Tool"><layout class="QVBoxLayout" name="layout"><item><widget class="QPushButton" name="runButton"><property name="text"><string>Run</string></property></widget></item><item><widget class="QLabel" name="resultLabel"><property name="text"><string>Waiting</string></property></widget></item></layout></widget><resources/><connections/></ui>'''
class ExtensionEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_ui_rejects_custom_widgets_external_resources_and_duplicate_names(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'tool.ui';path.write_text(UI,encoding='utf-8');self.assertIn('runButton',ValidateUI(path))
            for source in (UI.replace('QPushButton','QWebEngineView'),UI.replace('resultLabel','runButton'),UI.replace('<resources/>','<resources><include location="../external.qrc"/></resources>')):
                path.write_text(source,encoding='utf-8')
                with self.assertRaises(ExtensionError):ValidateUI(path)
    def test_menus_visual_manifest_and_manager_are_shared_theme_children(self):
        from Editor.gui.application import Editor
        from Editor.gui.extensions import ExtensionsDialog,PackageBuildDialog
        from bazzalt.settings import DataPaths
        with tempfile.TemporaryDirectory() as temp,patch.object(DataPaths,'Root',return_value=Path(temp)):
            window=Editor()
            try:
                self.assertEqual(window.MenuBar.BuildPackageAction.text(),'Build Package...')
                manager=ExtensionsDialog(window);self.assertIs(manager.parent(),window);self.assertEqual(manager.Tree.topLevelItemCount(),0);manager.close();manager.deleteLater()
                builder=PackageBuildDialog(window);self.assertIs(builder.parent(),window);self.assertEqual(builder.Target.count(),4);builder.close();builder.deleteLater()
            finally:window.Controller.SetDirty(False);window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_actual_controller_drives_ui_and_scene_fields_serialize(self):
        from Editor.runtime import _LoadNativeModule
        native=_LoadNativeModule()
        if not native or not hasattr(native,'ExtensionScript'):self.skipTest('native extension bridge is not built')
        from Editor.gui.application import Editor
        from bazzalt.settings import DataPaths
        with tempfile.TemporaryDirectory() as temp,patch.object(DataPaths,'Root',return_value=Path(temp)/'data'):
            root=Path(temp);source=root/'author';source.mkdir();(source/'tool.ui').write_text(UI,encoding='utf-8')
            (source/'controller.lua').write_text('''return {
OnClicked=function(self,name)
 if name=='runButton' then Editor:SetText('resultLabel','Done') end
end,
Open=function(self) Editor:OpenPanel('panel') end,
Values=function(self) Editor:SetValue('numberInput',7);assert(Editor:GetValue('numberInput')==7) end,
OnChanged=function(self,name) Editor:SetText('resultLabel','Changed') end,
Create=function(self) Editor:BeginUndo('Extension create');local scene=Bazzalt.SceneManager.GetActiveScene();scene:CreateEntity('Extension cube');Editor:EndUndo() end,
Fail=function(self) Editor:BeginUndo('Extension failed edit');Bazzalt.SceneManager.GetActiveScene():CreateEntity('Rolled back');error('Intentional failure') end
}''',encoding='utf-8')
            manifest={'General':{'Id':'studio.tool','Name':'Studio Tool','ShortDescription':'An integration fixture','Version':'1.0.0'},'Compatibility':{'MinEngineVersion':'0.7.0'},'AssetRegistry':[{'Id':'panel','Path':'tool.ui'},{'Id':'control','Path':'controller.lua'}],'EditorRegistry':{'panel':{'Title':'Tool','Script':'control'}},'Scripts':{'control':{'Id':'control','Entry':True}},'Extends':{'Menubar':[{'Menu':'Tools','Title':'Open Tool','Script':'control','Function':'Open'}],'SceneSettings':{'Amount':{'Type':'float','Default':1.0,'Min':0,'Max':5}}}}
            path=source/'extension.yaml';path.write_text(yaml.safe_dump(manifest),encoding='utf-8');package=BuildExtensionPackage(path,root/'tool.bpack',ROOT/'build/Editor/Release','windows' if os.name=='nt' else 'linux','x64');store=ExtensionStore(DataPaths.Root()/'Extensions','0.7.0','Windows' if os.name=='nt' else 'Linux');store.Import(package);store.SetEnabled('studio.tool',True)
            window=Editor()
            try:
                self.App.processEvents();self.assertEqual(len(window.Extensions.Panels),1)
                panel=window.Extensions.Panels[('studio.tool','panel')];panel.findChild(QPushButton,'runButton').click();self.assertEqual(panel.findChild(QLabel,'resultLabel').text(),'Done')
                number=QSpinBox(panel);number.setObjectName('numberInput');number.valueChanged.connect(lambda _:window.Extensions.Call('studio.tool','control','OnChanged','numberInput'))
                window.Extensions.Call('studio.tool','control','Values');self.assertEqual(number.value(),7);self.assertEqual(panel.findChild(QLabel,'resultLabel').text(),'Done')
                before=len(window.Runtime.Entities());window.Extensions.Call('studio.tool','control','Create');self.assertEqual(len(window.Runtime.Entities()),before+1);self.assertTrue(window.Controller.History.Undo());self.assertEqual(len(window.Runtime.Entities()),before);self.assertTrue(window.Controller.History.Redo());self.assertEqual(len(window.Runtime.Entities()),before+1)
                window.Extensions.Call('studio.tool','control','Fail');self.assertEqual(len(window.Runtime.Entities()),before+1);self.assertIn(('studio.tool','control'),window.Extensions.Failed)
                scene=window.Runtime.SceneInfo()['uuid'];window.Controller.SelectEntity(scene);self.assertIn('extension.studio.tool',window.Properties._sections)
                field=window.Properties._sections['extension.studio.tool']._fields['Amount'];field.setValue(2.5);self.assertEqual(json.loads(window.Runtime._host.scene_extension_properties(scene)['studio.tool:Amount']),2.5)
            finally:window.Controller.SetDirty(False);window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

    def test_manifest_edits_keep_string_and_number_types_and_block_invalid_build(self):
        from Editor.gui.application import Editor
        from Editor.gui.extensions import PackageBuildDialog
        from bazzalt.extensions import ReadManifest
        from bazzalt.settings import DataPaths
        with tempfile.TemporaryDirectory() as temp,patch.object(DataPaths,'Root',return_value=Path(temp)):
            window=Editor();dialog=PackageBuildDialog(window)
            try:
                dialog.Source=ROOT/'examples/extensions/math/extension.yaml';dialog.Manifest=ReadManifest(dialog.Source);dialog._Refresh()
                item=next(dialog.Sections.topLevelItem(i) for i in range(dialog.Sections.topLevelItemCount()) if dialog.Sections.topLevelItem(i).text(0)=='General');dialog.Sections.setCurrentItem(item)
                general=dialog.Fields.topLevelItem(0);name=next(general.child(i) for i in range(general.childCount()) if general.child(i).text(0)=='Name');name.setText(1,'Edited name');self.assertEqual(dialog.Manifest['General']['Name'],'Edited name')
                version=next(dialog.Sections.topLevelItem(i) for i in range(dialog.Sections.topLevelItemCount()) if dialog.Sections.topLevelItem(i).text(0)=='FormatVersion');dialog.Sections.setCurrentItem(version);dialog.Fields.topLevelItem(0).setText(1,'bad number');self.assertTrue(dialog.InvalidFields)
                with patch('Editor.gui.extensions.SaveFile') as save:dialog._Build();save.assert_not_called()
            finally:dialog.close();dialog.deleteLater();window.close();window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
