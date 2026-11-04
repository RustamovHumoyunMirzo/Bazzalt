import os,time,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService

class NativeLightingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_probe_inspector_and_transactional_bake(self):
        runtime=RuntimeService()
        if not runtime.IsAvailable():self.skipTest('Native runtime unavailable')
        try:
            if not hasattr(runtime._host,'begin_lighting_bake'):self.skipTest('Rebuild the native lighting bridge')
            probe=runtime.CreateEntity('Probe');self.assertTrue(runtime.AddComponent(probe,'Light Probe'));self.assertFalse(runtime.SetComponentProperty(probe,'Light Probe','Radius',0));self.assertTrue(runtime.SetComponentProperty(probe,'Light Probe','Radius',20));self.assertFalse(runtime.SetComponentProperty(probe,'Light Probe','Baked',True))
            light=runtime.CreateEntity('Baked light');self.assertTrue(runtime.AddComponent(light,'Light'));self.assertTrue(runtime.SetComponentProperty(light,'Light','Mode',1));self.assertFalse(runtime.SetComponentProperty(light,'Light','Mode',99))
            mesh=runtime.CreateEntity('Static geometry');self.assertTrue(runtime.AddComponent(mesh,'Primitive Object'));self.assertTrue(runtime.SetComponentProperty(mesh,'Primitive Object','Contribute GI',True))
            self.assertTrue(runtime.AddComponent(mesh,'Lightmap'));self.assertFalse(runtime.SetComponentProperty(mesh,'Lightmap','Resolution',17));self.assertTrue(runtime.SetComponentProperty(mesh,'Lightmap','Resolution',32));self.assertTrue(runtime.SetComponentProperty(mesh,'Lightmap','Padding',1))
            reflection=runtime.CreateEntity('Reflection');self.assertTrue(runtime.AddComponent(reflection,'Reflection Probe'));self.assertTrue(runtime.SetComponentProperty(reflection,'Reflection Probe','Resolution',4));self.assertFalse(runtime.SetComponentProperty(reflection,'Reflection Probe','Radius',0))
            runtime.LightingCommand('begin_lighting_bake',16,0,100,.002,(0,0,0),1,4,4096)
            deadline=time.monotonic()+10
            while runtime.LightingCommand('lighting_bake_info')['status']==0 and time.monotonic()<deadline:time.sleep(.001)
            self.assertEqual(runtime.LightingCommand('lighting_bake_info')['status'],1);self.assertTrue(runtime.LightingCommand('apply_lighting_bake'))
            data=runtime.EntityDetails(probe)['component_data']['Light Probe'];self.assertTrue(data['Baked']);self.assertEqual(set(data),{'Radius','Intensity','Baked'})
            self.assertTrue(runtime.EntityDetails(mesh)['component_data']['Lightmap']['Baked']);self.assertTrue(runtime.EntityDetails(reflection)['component_data']['Reflection Probe']['Baked'])
            runtime.LightingCommand('clear_lighting_bake');self.assertFalse(runtime.EntityDetails(probe)['component_data']['Light Probe']['Baked'])
            runtime.LightingCommand('begin_lighting_bake',16,0,100,.002,(0,0,0));self.assertTrue(runtime.SetComponentProperty(light,'Light','Intensity',123))
            deadline=time.monotonic()+10
            while runtime.LightingCommand('lighting_bake_info')['status']==0 and time.monotonic()<deadline:time.sleep(.001)
            self.assertFalse(runtime.LightingCommand('apply_lighting_bake'));self.assertIn('changed',runtime.LightingCommand('lighting_bake_info')['error'])
        finally:runtime.Release()
