import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
import tempfile,json,uuid,re
from pathlib import Path
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QEvent
from Editor.gui.application import Editor

class TerrainRuntimeEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Window=Editor()
        if not self.Window.Runtime.IsAvailable():self.skipTest('Native editor bridge is not built')
    def tearDown(self):
        self.Window.Controller.SetDirty(False);self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_inspector_layer_edits_reorder_and_undo_preserve_samples(self):
        w=self.Window;r=w.Runtime;e=r.CreateEntity('Terrain');self.assertTrue(r.AddComponent(e,'Terrain'));w.Controller.SelectEntity(e)
        fields=w.Properties._sections['runtime.Terrain']._fields;fields['Resolution'].SetValue(3);self.assertEqual(fields['ChunkSize'].GetValue(),2);self.assertEqual(r.EntityDetails(e)['component_data']['Terrain']['Resolution'],3)
        listing=w.Properties._sections['runtime.Terrain']._fields['Layers'];listing.Add()
        self.assertEqual(len(r.EntityDetails(e)['component_data']['Terrain']['Layers']),2)
        ray={'origin':(0,20,0),'direction':(0,-1,0)}
        self.assertTrue(r.TerrainCommand(e,'brush',dict(ray,layer=1,tool=0,radius=2.,strength=2.,seconds=1.))['changed'])
        listing._Changed(1,dict(listing.GetValue()[1],Name='Raised layer'))
        self.assertAlmostEqual(r.TerrainCommand(e,'hit',ray)['position'][1],2.,places=4)
        self.assertIs(w.Properties._sections['runtime.Terrain']._fields['Layers'],listing)
        listing.Remove(0);listing._Changed(0,dict(listing.GetValue()[0],Name='Kept samples'))
        self.assertAlmostEqual(r.TerrainCommand(e,'hit',ray)['position'][1],2.,places=4)
        self.assertEqual(listing.GetValue()[0]['_Index'],0)
        w.Controller.Undo();self.assertAlmostEqual(r.TerrainCommand(e,'hit',ray)['position'][1],2.,places=4)
    def test_vegetation_metadata_edits_keep_other_prototype_placements(self):
        w=self.Window;r=w.Runtime
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        root=Path(temporary.name);assets=root/'Assets';assets.mkdir();project=root/'Game.bproject'
        project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Terrain\nAssetDirectory: Assets\nStartupScene: ""\nProperties: {{}}\n',encoding='utf-8')
        model=assets/'Tree.gltf';model.write_text(json.dumps({'asset':{'version':'2.0'},'nodes':[{'name':'Tree'}],'scenes':[{'nodes':[0]}],'scene':0}),encoding='utf-8')
        self.assertTrue(r.LoadProject(project));asset=r.AssetInfo(model);self.assertTrue(asset.get('uuid'))
        e=r.CreateEntity('Ground');self.assertTrue(r.AddComponent(e,'Terrain'));w.Controller.SelectEntity(e)
        listing=w.Properties._sections['runtime.Terrain']._fields['Vegetation'];listing.Add();listing._Changed(0,dict(listing.GetValue()[0],Model=asset['uuid'],MaxInstances=4,Density=1.,Spacing=.1))
        self.assertTrue(r.TerrainCommand(e,'generate',{'prototype':0})['changed'])
        def count():
            match=re.search(r'''Instances\.Count["']?:\s*["']?(\d+)''',r.CaptureScene().decode('utf-8'));self.assertIsNotNone(match);return int(match[1])
        self.assertEqual(count(),4);listing.Add();self.assertEqual(count(),4);listing.Move(0,1);self.assertEqual(count(),4)
        listing._Changed(1,dict(listing.GetValue()[1],Enabled=False));self.assertEqual(count(),4)
        listing.Remove(0);self.assertEqual(count(),4);listing.Remove(0);self.assertEqual(count(),0)

if __name__=='__main__':unittest.main()
