import os,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QEvent
from Editor.gui.application import Editor
from Editor.theme import ThemeManager
from Editor.gui.physics import CollisionLayersDialog

class PhysicsInspectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Window=Editor(ThemeManager(self.App))
        if not self.Window.Runtime.IsAvailable():self.skipTest('native bridge is not built')
    def tearDown(self):
        self.Window.Controller.SetDirty(False);self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_components_gravity_shape_and_scene_settings(self):
        w=self.Window;r=w.Runtime;e=r.CreateEntity('Physics body')
        for component in ('Rigidbody','Shape Collider','Mesh Collider'):self.assertTrue(r.AddComponent(e,component))
        w.Controller.SelectEntity(e,force=True)
        self.assertIn('runtime.Rigidbody',w.Properties._sections)
        self.assertIn('runtime.Shape Collider',w.Properties._sections)
        self.assertNotIn('Gravity',r.EntityDetails(e)['component_data']['Rigidbody'])
        self.assertTrue(r.SetComponentProperty(e,'Rigidbody','Use Scene Gravity',False))
        self.assertIn('Gravity',r.EntityDetails(e)['component_data']['Rigidbody'])
        self.assertTrue(r.SetComponentProperty(e,'Shape Collider','Shape',1))
        w.Controller.SelectEntity(e,force=True)
        self.assertIn('Radius',w.Properties._sections['runtime.Shape Collider']._fields)
        self.assertNotIn('Size',w.Properties._sections['runtime.Shape Collider']._fields)
        identity=str(r.SceneInfo()['uuid']);settings=r.ScenePhysics(identity)
        self.assertAlmostEqual(settings['Gravity'][1],-9.81,places=5)
        self.assertTrue(r.SetScenePhysics(identity,{'Gravity':(0,-4,0)}))
        self.assertEqual(tuple(r.ScenePhysics(identity)['Gravity']),(0.,-4.,0.))
        self.assertFalse(r.SetScenePhysics(identity,{'Max Bodies':0}))
        dialog=CollisionLayersDialog(w,lambda key:key,[0xffffffff]*32)
        dialog.Table.item(2,7).setCheckState(__import__('PySide6.QtCore',fromlist=['Qt']).Qt.CheckState.Unchecked)
        self.assertFalse(dialog.Masks[2]&(1<<7));self.assertFalse(dialog.Masks[7]&(1<<2));dialog.deleteLater()
