import copy,json,os,tempfile,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from PySide6.QtCore import QPointF
from PySide6.QtWidgets import QApplication
from Editor import vfx_graph as G
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.theme import ThemeManager
from Editor.gui.panels.vfx_graph import VFXGraphPanel
from Editor.gui.panels.shader_graph import ShaderGraphPanel
from Editor.runtime import RuntimeService,_LoadNativeModule
class VFXGraphTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_roundtrip_and_hostile_documents(self):
        graph=G.NewGraph()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'effect.vfx';self.assertTrue(G.Save(path,graph));self.assertFalse(G.Save(path,graph));self.assertEqual(G.Load(path),graph)
        for mutate in (lambda g:g['nodes'][0]['inputs'].__setitem__('in',g['nodes'][-1]['id']),lambda g:g['nodes'][1]['inputs'].__setitem__('in','missing'),lambda g:g['nodes'][1]['values'].__setitem__('cone',float('nan')),lambda g:g['nodes'][1].__setitem__('id',g['nodes'][0]['id']),lambda g:g['nodes'][-1]['values'].__setitem__('texture','../outside.png')):
            bad=copy.deepcopy(graph);mutate(bad)
            with self.assertRaises(ValueError):G.Validate(bad)
    def test_collapsed_palettes_search_and_vfx_history(self):
        resources=ResourceManager();locale=LocalizationManager(resources);theme=ThemeManager(self.App)
        for cls in (ShaderGraphPanel,VFXGraphPanel):
            panel=cls(None,locale,resources,theme)
            try:
                self.assertTrue(all(not panel.Palette.topLevelItem(i).isExpanded() for i in range(panel.Palette.topLevelItemCount())))
                panel.Search.setText('Spawn' if cls is VFXGraphPanel else 'Sine');self.assertTrue(any(panel.Palette.topLevelItem(i).isExpanded() for i in range(panel.Palette.topLevelItemCount())));panel.Search.clear();self.assertTrue(all(not panel.Palette.topLevelItem(i).isExpanded() for i in range(panel.Palette.topLevelItemCount())))
                if cls is ShaderGraphPanel:continue
                panel.Preview=lambda:None
                with tempfile.TemporaryDirectory() as folder:
                    path=Path(folder)/'effect.vfx';G.Save(path,G.NewGraph());self.assertTrue(panel.Open(path));panel.Add('Drag',QPointF(400,200));identity=panel.Graph['nodes'][-1]['id'];self.assertIn(identity,panel.Nodes);output=next(n for n in panel.Graph['nodes'] if n['type']=='Output');panel.Connect(output['inputs']['in'],identity,'in');panel.Connect(identity,output['id'],'in');self.assertEqual(G.Load(path)['nodes'][-2]['inputs']['in'],identity)
                    panel.History.undo();self.assertNotEqual(next(n for n in panel.Graph['nodes'] if n['type']=='Output')['inputs']['in'],identity);panel.History.redo();self.assertEqual(next(n for n in panel.Graph['nodes'] if n['type']=='Output')['inputs']['in'],identity)
                    panel.SelectNodes({output['id']});panel.Delete();self.assertEqual(sum(n['type']=='Output' for n in panel.Graph['nodes']),1)
            finally:panel.deleteLater();self.App.processEvents()
    def test_clearing_an_output_asset_stores_zero_uuid(self):
        from Editor.gui.widgets.fields import AssetPickerInput
        resources=ResourceManager();panel=VFXGraphPanel(None,LocalizationManager(resources),resources,ThemeManager(self.App));panel.Preview=lambda:None
        try:
            with tempfile.TemporaryDirectory() as folder:
                graph=G.NewGraph();output=next(n for n in graph['nodes'] if n['type']=='Output');output['values']['material']='12345678-1234-4234-8234-123456789abc';path=Path(folder)/'effect.vfx';G.Save(path,graph);self.assertTrue(panel.Open(path));panel.SelectNodes({output['id']});pickers=panel.Fields.findChildren(AssetPickerInput);self.assertEqual(len(pickers),2);pickers[0].Clear();self.assertEqual(next(n for n in G.Load(path)['nodes'] if n['type']=='Output')['values']['material'],'00000000-0000-0000-0000-000000000000')
        finally:panel.deleteLater();self.App.processEvents()

    def test_native_preview_and_component_authoring(self):
        module=_LoadNativeModule()
        if not module or not hasattr(module,'preview_vfx'):self.skipTest('Rebuild the native bridge')
        points=module.preview_vfx(G.Generate(G.NewGraph()),.5);self.assertTrue(points)
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime missing')
        try:
            entity=runtime.CreateEntity('Particles');self.assertTrue(runtime.AddComponent(entity,'Particle Emitter'));details=runtime.EntityDetails(entity);self.assertIn('Particle Emitter',details['components']);self.assertTrue(runtime.SetComponentProperty(entity,'Particle Emitter','Max Particles',512));self.assertFalse(runtime.SetComponentProperty(entity,'Particle Emitter','Max Particles',0));self.assertTrue(runtime.SetComponentEnabled(entity,'Particle Emitter',False));self.assertFalse(runtime.EntityDetails(entity)['component_enabled']['Particle Emitter'])
        finally:runtime.Release()
if __name__=='__main__':unittest.main()
