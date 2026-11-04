import copy,os,tempfile,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from PySide6.QtWidgets import QApplication
from Editor import animation as A
from Editor.gui.panels.animation import AnimationPanel
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.theme import ThemeManager
from Editor.runtime import RuntimeService
class Runtime:
    def __init__(self):self.Previews=[];self.Ends=0
    def AnimationInfo(self,entity):return dict(bones=[dict(index=0,name='Root',parent=-1)],clips=[dict(name='Walk',duration=1,tracks=[dict(bone=0,channel=0,keys=[0,.5,1])])])
    def PreviewAnimation(self,*args):self.Previews.append(args)
    def EndAnimationPreview(self):self.Ends+=1
class AnimationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_controller_validation_and_roundtrip(self):
        data=A.NewController()
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'move.banim';self.assertTrue(A.Save(path,data));self.assertEqual(A.Load(path),data)
        bad=copy.deepcopy(data);bad['states'].append(copy.deepcopy(bad['states'][0]))
        with self.assertRaises(ValueError):A.Validate(bad)
        bad=copy.deepcopy(data);bad['states'][0]['speed']=float('nan')
        with self.assertRaises(ValueError):A.Validate(bad)
        bad=copy.deepcopy(data);bad['transitions']=[{'from':'Idle','to':'Missing'}]
        with self.assertRaises(ValueError):A.Validate(bad)
    def test_graph_history_transitions_and_preview_restore(self):
        resources=ResourceManager();runtime=Runtime();panel=AnimationPanel(runtime,LocalizationManager(resources),resources,ThemeManager(self.App))
        try:
            panel.Entity='model';panel.Refresh();self.assertEqual(panel.Clips.count(),1);panel.Time.setValue(.5);self.assertEqual(runtime.Previews[-1],('model','Walk',.5))
            with tempfile.TemporaryDirectory() as root:
                path=Path(root)/'move.banim';A.Save(path,A.NewController());self.assertTrue(panel.Open(path));panel.NewState();self.assertEqual(len(A.Load(path)['states']),2);panel.Selected='Idle';panel.NewTransition();self.assertEqual(A.Load(path)['transitions'][0]['from'],'Idle');panel.History.undo();self.assertFalse(A.Load(path)['transitions']);panel.History.redo();self.assertTrue(A.Load(path)['transitions']);panel.SetEntity('another');self.assertGreater(runtime.Ends,0)
        finally:panel.EndPreview();panel.deleteLater();self.App.processEvents()
    def test_native_preview_restores_authoring_state_before_save(self):
        import struct,json
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        try:
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);assets=root/'Assets';assets.mkdir();(root/'Game.bproject').write_text('FormatVersion: 1\nProjectUUID: "12345678-1234-4234-8234-123456789abc"\nName: Animation\nAssetDirectory: Assets\nStartupScene: ""\n')
                model=dict(asset={'version':'2.0'},buffers=[dict(uri='keys.bin',byteLength=32)],bufferViews=[dict(buffer=0,byteOffset=0,byteLength=8),dict(buffer=0,byteOffset=8,byteLength=24)],accessors=[dict(bufferView=0,componentType=5126,count=2,type='SCALAR'),dict(bufferView=1,componentType=5126,count=2,type='VEC3')],nodes=[dict(name='Joint')],scenes=[dict(nodes=[0])],scene=0,animations=[dict(name='Move',samplers=[dict(input=0,output=1)],channels=[dict(sampler=0,target=dict(node=0,path='translation'))])]);(assets/'model.gltf').write_text(json.dumps(model));(assets/'keys.bin').write_bytes(struct.pack('<8f',0,1,0,0,0,1,0,0));self.assertTrue(runtime.LoadProject(root/'Game.bproject'));entity=runtime.InstantiateModelPath(assets/'model.gltf');self.assertTrue(entity);joint=next(row['uuid'] for row in runtime.Entities() if row['name']=='Joint');self.assertEqual(runtime.AnimationInfo(entity)['clips'][0]['name'],'Move');runtime.PreviewAnimation(entity,'Move',.5);self.assertAlmostEqual(runtime.EntityDetails(joint)['position'][0],.5,places=4);runtime.EndAnimationPreview();self.assertAlmostEqual(runtime.EntityDetails(joint)['position'][0],0,places=4);self.assertNotIn('Animator',runtime.EntityDetails(entity)['components']);controller=json.dumps(dict(version=1,entry='Preview',states=[dict(name='Preview',clip='Move')],transitions=[]));status=runtime.PreviewAnimationController(entity,controller,.25,{},True);self.assertEqual(status['state'],'Preview');self.assertAlmostEqual(runtime.EntityDetails(joint)['position'][0],.25,places=4);runtime.EndAnimationPreview();mapping=runtime.AnimationRetargetMap(entity,runtime.AnimationInfo(entity)['model']);self.assertEqual(len(mapping),1);self.assertTrue(mapping[0]['copyTranslation']);runtime.PreviewAnimation(entity,'Move',.75);self.assertTrue(runtime.SaveScene(assets/'Saved.bscene'));self.assertAlmostEqual(runtime.EntityDetails(joint)['position'][0],0,places=4);self.assertNotIn('Animator',runtime.EntityDetails(entity)['components']);runtime.Release()
        finally:runtime.Release()

    def test_native_animator_and_ik_inspector(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        try:
            entity=runtime.CreateEntity('Animation');self.assertTrue(runtime.AddComponent(entity,'Animator'));self.assertTrue(runtime.SetComponentProperty(entity,'Animator','Speed',2.));self.assertFalse(runtime.SetComponentProperty(entity,'Animator','Speed',-1.));self.assertTrue(runtime.AddComponent(entity,'IK Constraint'));self.assertIn('Root Bone',runtime.EntityDetails(entity)['component_data']['Animator']);self.assertEqual(runtime.AnimationInfo(entity)['clips'],[])
        finally:runtime.Release()
if __name__=='__main__':unittest.main()
