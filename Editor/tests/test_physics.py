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
        picker=w.Properties._sections['runtime.Mesh Collider']._fields['Mesh Asset'];self.assertIn('automatic',picker.Display.placeholderText());self.assertIsNone(picker.GetValue())
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

    def test_joint_and_soft_body_fields(self):
        w=self.Window;r=w.Runtime;body=r.CreateEntity('Anchor');r.AddComponent(body,'Rigidbody')
        joint=r.CreateEntity('Door');r.AddComponent(joint,'Rigidbody')
        for kind in ('Joint','Distance Joint','Hinge Joint','Slider Joint','Ball And Socket Joint','Fixed Joint'):
            self.assertTrue(r.AddComponent(joint,kind));self.assertTrue(r.SetComponentProperty(joint,kind,'Connected Body',body))
        w.Controller.SelectEntity(joint,force=True)
        fields=w.Properties._sections['runtime.Hinge Joint']._fields
        self.assertIn('Connected Body',fields);self.assertIn('Motor',fields);self.assertNotIn('Min Distance',fields)
        self.assertEqual(fields['Connected Body'].GetValue(),body)
        for kind in ('Cloth','Rope'):
            entity=r.CreateEntity(kind);
            if kind=='Cloth':self.assertTrue(r.AddComponent(entity,'Primitive Object'))
            self.assertTrue(r.AddComponent(entity,kind));w.Controller.SelectEntity(entity,force=True)
            self.assertIn('Material Asset',w.Properties._sections['runtime.'+kind]._fields)
            self.assertIn('Mass',r.EntityDetails(entity)['component_data'][kind])
            if kind=='Cloth':self.assertNotIn('Width',w.Properties._sections['runtime.Cloth']._fields)
            self.assertFalse(r.SetComponentProperty(entity,kind,'Mass',-1))
            self.assertTrue(r.RemoveComponent(entity,kind))

    def test_cloth_addition_keeps_initialized_editor_responsive(self):
        r=self.Window.Runtime;self.assertTrue(r.Play());r.Stop()
        existing=r.CreateEntity('Keep in hierarchy');body=r.CreateEntity('Rigid body');self.assertTrue(r.AddComponent(body,'Rigidbody'))
        self.assertFalse(r.AddComponent(body,'Cloth'));self.assertNotIn('Cloth',r.EntityDetails(body)['components'])
        self.assertEqual({item['uuid'] for item in r.Entities()},{existing,body});self.assertTrue(r.Tick())
        fabric=r.CreateEntity('Cloth');self.assertTrue(r.AddComponent(fabric,'Primitive Object'));self.assertTrue(r.AddComponent(fabric,'Cloth'));self.assertTrue(r.Tick())
        for component in ('Rigidbody','Shape Collider','Mesh Collider','Rope'):self.assertFalse(r.AddComponent(fabric,component))
        self.assertTrue(r.Tick());self.assertEqual({item['uuid'] for item in r.Entities()},{existing,body,fabric})
        self.assertTrue(r.RemoveComponent(fabric,'Cloth'));self.assertTrue(r.AddComponent(fabric,'Rope'));self.assertTrue(r.Tick())

    def test_cloth_pins_none_clears_and_restores_constraints(self):
        w=self.Window;r=w.Runtime;entity=r.CreateEntity('Cloth pins');r.AddComponent(entity,'Primitive Object');r.AddComponent(entity,'Cloth');w.Controller.SelectEntity(entity,force=True)
        field=w.Properties._sections['runtime.Cloth']._fields['Pin Edges'];none=field.menu().actions()[0]
        self.assertEqual(none.text(),w.Localization.Translate('physics.none'));self.assertEqual(field.GetValues(),(4,));self.assertFalse(none.isChecked())
        none.trigger();self.assertEqual(r.EntityDetails(entity)['component_data']['Cloth']['Pin Edges'],0);self.assertEqual(field.GetValues(),());self.assertTrue(none.isChecked())
        next(action for action in field.menu().actions() if action.data()==4).trigger()
        self.assertEqual(r.EntityDetails(entity)['component_data']['Cloth']['Pin Edges'],4);self.assertEqual(field.GetValues(),(4,));self.assertFalse(none.isChecked())

    def test_cloth_material_controls_preserve_small_compliance(self):
        w=self.Window;r=w.Runtime;entity=r.CreateEntity('Flexible cloth');r.AddComponent(entity,'Primitive Object');r.AddComponent(entity,'Cloth');w.Controller.SelectEntity(entity,force=True)
        fields=w.Properties._sections['runtime.Cloth']._fields
        self.assertAlmostEqual(fields['Compliance'].GetValue(),.0001,places=8);self.assertEqual(fields['Bend Mode'].GetValue(),0)
        fields['Compliance'].setValue(.00001);self.assertAlmostEqual(r.EntityDetails(entity)['component_data']['Cloth']['Compliance'],.00001,places=8)
        self.assertIn('Pressure',fields);self.assertIn('Collision Radius',fields);self.assertIn('Subdivision Level',fields)

    def test_soft_body_gizmo_tracks_play_and_restores_on_stop(self):
        from PySide6.QtTest import QTest
        w=self.Window;r=w.Runtime;entity=r.CreateEntity('Moving cloth')
        r.AddComponent(entity,'Primitive Object');r.AddComponent(entity,'Cloth')
        r.SetComponentProperty(entity,'Cloth','Pin Edges',0)
        w.Controller.SelectEntity(entity,force=True)
        original=tuple(r.EntityPose(entity)['world_position'])
        self.assertTrue(r.Play());r.Pause(True)
        for _ in range(30):QTest.qWait(20);r.Step()
        w.Controller._UpdateGizmo()
        position=tuple(r.EntityPose(entity)['world_position'])
        self.assertLess(position[1],original[1]-.5)
        self.assertAlmostEqual(w.Scene.Surface._selection.Y,position[1],places=4)
        self.assertTrue(r.Translate(entity,(2,1,0)))
        w.Controller._UpdateGizmo()
        moved=tuple(r.EntityPose(entity)['world_position'])
        self.assertAlmostEqual(moved[0],position[0]+2,places=4)
        self.assertAlmostEqual(w.Scene.Surface._selection.Y,position[1]+1,places=4)
        r.Stop();w.Controller._UpdateGizmo()
        self.assertEqual(tuple(r.EntityPose(entity)['world_position']),original)
        self.assertAlmostEqual(w.Scene.Surface._selection.Y,original[1],places=4)

    def test_material_clock_freezes_in_paused_play(self):
        r=self.Window.Runtime;self.assertTrue(r.Play());r.Tick();before=r.Statistics()['material_time'];self.assertGreaterEqual(before,0)
        r.Pause(True)
        from PySide6.QtTest import QTest
        QTest.qWait(20);r.Tick();paused=r.Statistics()['material_time'];QTest.qWait(20);r.Tick();self.assertEqual(r.Statistics()['material_time'],paused)
        r.Pause(False);QTest.qWait(20);r.Tick();self.assertGreater(r.Statistics()['material_time'],paused);r.Stop()
