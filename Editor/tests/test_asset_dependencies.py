import os,tempfile,unittest,uuid,json
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication,QListWidget
from PySide6.QtCore import Qt
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.properties import PropertiesPanel
from Editor.gui.controller import EditorController
from Editor.runtime import RuntimeService
class AssetDependencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_inspector_reads_cached_graph_and_navigates_without_ids(self):
        locale=LocalizationManager(ResourceManager());panel=PropertiesPanel(locale)
        try:
            with tempfile.TemporaryDirectory() as folder:
                path=Path(folder)/'asset.matinst';path.write_text('{}');target=Path(folder)/'image.png'
                provider=Mock(return_value={'state':2,'reference_count':251,'references':[{'path':str(target),'field':'properties/albedo','state':2}], 'referencers':[{'path':str(path)}]})
                c=SimpleNamespace(Runtime=SimpleNamespace(AssetDependencies=provider),Window=SimpleNamespace(Properties=panel,Localization=locale),SelectAsset=Mock())
                EditorController._InspectAssetDependencies(c,path)
                provider.assert_called_once_with(path);lists=panel.findChildren(QListWidget);self.assertEqual(len(lists),2)
                self.assertEqual(panel._sections['asset.references']._fields[locale.Translate('assets.dependencies')].text(),'251')
                self.assertEqual(lists[0].item(0).text(),'image.png');self.assertIn('albedo',lists[0].item(0).toolTip())
                lists[0].itemDoubleClicked.emit(lists[0].item(0));c.SelectAsset.assert_called_once_with(str(target))
        finally:panel.deleteLater();self.App.processEvents()
    def test_native_graph_refresh_and_public_identity(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        with tempfile.TemporaryDirectory() as folder:
            try:
                root=Path(folder);assets=root/'Assets';assets.mkdir();target=assets/'data.bin';target.write_bytes(b'content')
                project=root/'Game.bproject';project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: References\nAssetDirectory: Assets\nStartupScene: ""\n',encoding='utf-8')
                self.assertTrue(runtime.LoadProject(project));info=runtime.AssetInfo(str(target));self.assertTrue(info)
                material=assets/'source.matinst';material.write_text('{"shader":"'+info['uuid']+'"}',encoding='utf-8');self.assertTrue(runtime.RefreshAsset(material))
                report=runtime.AssetDependencies(material);self.assertEqual(report['references'][0]['target'],info['uuid']);self.assertEqual(report['references'][0]['state'],2)
                self.assertEqual(runtime.AssetDependencies(target)['referencers'][0]['path'].replace('\\','/'),str(material).replace('\\','/'))
                material.write_text('{"shader":null}',encoding='utf-8');self.assertTrue(runtime.RefreshAsset(material));self.assertEqual(runtime.AssetDependencies(target)['referencers'],[])
                large=assets/'large.matinst';large.write_text(json.dumps({'shader':info['uuid'],'properties':{f'texture{i}':info['uuid'] for i in range(250)}}),encoding='utf-8');self.assertTrue(runtime.RefreshAsset(large))
                summary=runtime.AssetDependencies(large);self.assertEqual(summary['reference_count'],251);self.assertEqual(len(summary['references']),200)
            finally:runtime.Release()
if __name__=='__main__':unittest.main()
