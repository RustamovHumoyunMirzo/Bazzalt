"""Real asynchronous pixel transfer used by native Wayland presentation."""
import os
import time
import unittest
from PySide6.QtWidgets import QApplication
from Editor.runtime import RuntimeService

@unittest.skipUnless(os.environ.get('BAZZALT_CAPTURE_TEST')=='1','Explicit native GPU capture test')
class CapturedViewportTests(unittest.TestCase):
    def test_render_resize_and_release_inflight_frame(self):
        app=QApplication.instance() or QApplication([]);runtime=RuntimeService();self.assertTrue(runtime.IsAvailable(),runtime.LastError());self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
        try:
            self.assertTrue(runtime.CreateCapturedViewport(500,True,96,64,1),runtime.LastError())
            sequence=0
            for width,height in ((96,64),(128,80)):
                runtime.ResizeViewport(500,width,height,1);deadline=time.monotonic()+20;frame=None
                while time.monotonic()<deadline:
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents();candidate=runtime.CapturedViewportFrame(500,sequence)
                    if candidate:
                        sequence=candidate['sequence']
                        if (candidate['width'],candidate['height'])==(width,height):frame=candidate;break
                    time.sleep(.01)
                self.assertIsNotNone(frame);self.assertEqual(len(frame['pixels']),width*height*4);self.assertTrue(any(frame['pixels'][3::4]))
            runtime.Tick();runtime.DestroyViewport(500)
        finally:runtime.Release()

    def test_game_without_a_camera_presents_opaque_black(self):
        app=QApplication.instance() or QApplication([]);runtime=RuntimeService()
        self.assertTrue(runtime.IsAvailable(),runtime.LastError());self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
        try:
            self.assertTrue(runtime.CreateCapturedViewport(501,False,96,64,1),runtime.LastError());deadline=time.monotonic()+20;frame=None
            while time.monotonic()<deadline:
                self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents();frame=runtime.CapturedViewportFrame(501,0)
                if frame:break
                time.sleep(.01)
            self.assertIsNotNone(frame);pixels=frame['pixels'];self.assertEqual(len(pixels),96*64*4)
            self.assertTrue(all(pixels[index]<3 for index in range(len(pixels)) if index%4!=3));self.assertTrue(all(value==255 for value in pixels[3::4]))
        finally:runtime.Release()

    def test_editor_camera_override_and_none_preserve_authored_state(self):
        app=QApplication.instance() or QApplication([]);runtime=RuntimeService();self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
        try:
            camera=runtime.CreateEntity('Preview camera');self.assertTrue(runtime.AddComponent(camera,'Camera'))
            other=runtime.CreateEntity('Other camera');self.assertTrue(runtime.AddComponent(other,'Camera'));runtime.Translate(other,(100,0,0))
            cube=runtime.CreateEntity('Cube');self.assertTrue(runtime.AddComponent(cube,'Primitive Object'));runtime.Translate(cube,(0,0,-4))
            light=runtime.CreateEntity('Light');runtime.AddComponent(light,'Light')
            before=runtime.CaptureScene();self.assertTrue(runtime.CreateCapturedViewport(503,False,96,64,1))
            def frame(after=0):
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents();value=runtime.CapturedViewportFrame(503,after)
                    if value:return value
                    time.sleep(.01)
                self.fail('Preview frame timed out')
            runtime.SetGamePreview(camera);first=frame();self.assertEqual(runtime.CaptureScene(),before)
            runtime.SetGamePreview(other);second=frame(first['sequence']);self.assertNotEqual(first['pixels'],second['pixels']);self.assertEqual(runtime.CaptureScene(),before)
            runtime.SetGamePreview('none');black=frame(second['sequence']);self.assertTrue(all(black['pixels'][i]<3 for i in range(len(black['pixels'])) if i%4!=3));self.assertFalse(runtime.HasGameOutput())
            runtime.SetGamePreview('');self.assertTrue(runtime.HasGameOutput());self.assertEqual(runtime.CaptureScene(),before)
            with self.assertRaises((ValueError,RuntimeError)):runtime.SetGamePreview('invalid-camera-id')
            self.assertTrue(runtime.HasGameOutput())
        finally:runtime.Release()

    def test_inspector_volumetric_mode_is_visible_in_scene_and_game(self):
        from PySide6.QtCore import QEvent
        from Editor.gui.application import Editor
        app=QApplication.instance() or QApplication([])
        runtime=RuntimeService()
        window=Editor(runtime=runtime)
        try:
            window.Controller.Timer.stop()
            self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
            cube=runtime.CreateEntity('Close fog receiver');self.assertTrue(runtime.AddComponent(cube,'Primitive Object'))
            camera=runtime.CreateEntity('Game camera');self.assertTrue(runtime.AddComponent(camera,'Camera'))
            self.assertTrue(runtime.SetTransform(camera,(0,0,5),(0,0,0,1),(1,1,1)))
            self.assertTrue(runtime.CreateCapturedViewport(505,True,128,96,1))
            self.assertTrue(runtime.CreateCapturedViewport(506,False,128,96,1))
            runtime.SetSceneCamera(505,(0,0,5),(0,0,0));runtime.SetGrid(False,1);runtime.SetEditorOrientationVisible(False);runtime.SetEditorIconsVisible(False)
            scene=runtime.SceneInfo()['uuid']
            self.assertTrue(runtime.SetSceneEnvironment(scene,{'fog_enabled':False}))
            sequences={505:0,506:0}
            def capture():
                counts={505:0,506:0};latest={};deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                    for viewport in sequences:
                        value=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                        if value:
                            sequences[viewport]=value['sequence'];counts[viewport]+=1;latest[viewport]=value['pixels']
                    if all(count>=3 for count in counts.values()):return latest
                    time.sleep(.01)
                self.fail('Fog viewport capture timed out')
            clear=capture()
            window.Controller.SelectEntity(scene,force=True)
            fog=window.Properties._sections['scene.fog'];fog.Enabled.setChecked(True)
            fog._fields[window.Localization.Translate('fog.mode')].SetValue(2)
            app.processEvents()
            mist=capture()
            for viewport in sequences:
                changed=sum(max(abs(clear[viewport][index+channel]-mist[viewport][index+channel]) for channel in range(3))>12 for index in range(0,len(clear[viewport]),4))
                self.assertGreater(changed,128*96//2,viewport)
        finally:
            window.Controller.SetDirty(False);window.close();window.deleteLater();app.sendPostedEvents(None,QEvent.Type.DeferredDelete);runtime.Release()

    def test_blended_foliage_fog_follows_camera_distance_and_preserves_alpha_holes(self):
        import json,struct,tempfile,uuid
        from pathlib import Path
        from PySide6.QtGui import QImage,QColor
        app=QApplication.instance() or QApplication([])
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);assets=root/'Assets';assets.mkdir()
            project=root/'Fog.bproject'
            project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Fog"\nAssetDirectory: "Assets"\nStartupScene: ""\nProperties:\n',encoding='utf-8')
            image=QImage(16,16,QImage.Format.Format_RGBA8888);image.fill(QColor(0,0,255,0))
            for y in range(16):
                for x in range(8,16):image.setPixelColor(x,y,QColor(0,0,255,255))
            image.save(str(assets/'coverage.png'))
            positions=(-2,-2,0,2,-2,0,2,2,0,-2,2,0)
            (assets/'quad.bin').write_bytes(struct.pack('<12f8f6H',*positions,0,1,1,1,1,0,0,0,0,1,2,0,2,3))
            model=dict(asset={'version':'2.0'},extensionsUsed=['KHR_materials_unlit'],buffers=[dict(uri='quad.bin',byteLength=92)],bufferViews=[dict(buffer=0,byteOffset=0,byteLength=48),dict(buffer=0,byteOffset=48,byteLength=32),dict(buffer=0,byteOffset=80,byteLength=12)],accessors=[dict(bufferView=0,componentType=5126,count=4,type='VEC3',min=[-2,-2,0],max=[2,2,0]),dict(bufferView=1,componentType=5126,count=4,type='VEC2'),dict(bufferView=2,componentType=5123,count=6,type='SCALAR')],images=[dict(uri='coverage.png')],textures=[dict(source=0)],materials=[dict(alphaMode='BLEND',doubleSided=True,pbrMetallicRoughness=dict(baseColorFactor=[1,1,1,.7],baseColorTexture=dict(index=0)),extensions={'KHR_materials_unlit':{}})],meshes=[dict(primitives=[dict(attributes={'POSITION':0,'TEXCOORD_0':1},indices=2,material=0)])],nodes=[dict(mesh=0)],scenes=[dict(nodes=[0])],scene=0)
            (assets/'Foliage.gltf').write_text(json.dumps(model),encoding='utf-8')
            runtime=RuntimeService()
            try:
                self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
                self.assertTrue(runtime.LoadProject(project),runtime.LastError())
                self.assertTrue(runtime.InstantiateModelPath(assets/'Foliage.gltf'))
                camera=runtime.CreateEntity('Game camera');self.assertTrue(runtime.AddComponent(camera,'Camera'))
                self.assertTrue(runtime.SetTransform(camera,(0,0,5),(0,0,0,1),(1,1,1)))
                self.assertTrue(runtime.CreateCapturedViewport(507,True,128,96,1))
                self.assertTrue(runtime.CreateCapturedViewport(508,False,128,96,1))
                # A project without a startup scene creates a starter camera.
                # Preview this test camera explicitly instead of relying on UUID order.
                runtime.SetGamePreview(camera)
                runtime.SetSceneCamera(507,(0,0,5),(0,0,0));runtime.SetGrid(False,1);runtime.SetEditorIconsVisible(False);runtime.SetEditorOrientationVisible(False)
                scene=runtime.SceneInfo()['uuid']
                self.assertTrue(runtime.SetSceneEnvironment(scene,{'fog_mode':2,'fog_enabled':True,'fog_sky':True,'fog_color':[1.,0.,0.],'fog_Density':.02,'fog_StartDistance':0.,'fog_MaxDistance':200.,'fog_HeightFalloff':0.,'fog_NoiseAmount':0.}))
                sequences={507:0,508:0};leaf=(48*128+68)*4;hole=(48*128+48)*4
                def capture(ready):
                    deadline=time.monotonic()+30;counts={507:0,508:0};frames={}
                    while time.monotonic()<deadline:
                        self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                        for viewport in sequences:
                            frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                            if frame:sequences[viewport]=frame['sequence'];counts[viewport]+=1;frames[viewport]=frame['pixels']
                        if all(count>=4 for count in counts.values()) and ready(frames):return frames
                        time.sleep(.01)
                    self.fail('Foliage capture did not settle: '+str({key:(list(value[leaf:leaf+4]),list(value[hole:hole+4])) for key,value in frames.items()}))
                near=capture(lambda frames:all(pixels[leaf+2]>100 for pixels in frames.values()))
                for viewport,pixels in near.items():
                    self.assertLess(pixels[leaf],90,(viewport,list(pixels[leaf:leaf+4]),list(pixels[hole:hole+4])))
                    self.assertGreater(pixels[hole],200,(viewport,list(pixels[leaf:leaf+4]),list(pixels[hole:hole+4])))
                runtime.SetSceneCamera(507,(0,0,20),(0,0,0))
                # Different camera positions must not leak through shared fog views.
                capture(lambda frames:frames[507][leaf]>near[507][leaf]+35 and abs(frames[508][leaf]-near[508][leaf])<4)
                self.assertTrue(runtime.SetTransform(camera,(0,0,20),(0,0,0,1),(1,1,1)))
                far=capture(lambda frames:all(pixels[leaf]>near[key][leaf]+35 for key,pixels in frames.items()))
                for viewport,pixels in far.items():
                    self.assertGreater(pixels[leaf],near[viewport][leaf]+35)
                    self.assertAlmostEqual(pixels[hole],near[viewport][hole],delta=4)
                # Steady rendering must reuse GPU material instances and targets.
                before=runtime.Statistics()
                self.assertGreater(before['fog_cached_materials'],0)
                for _ in range(40):
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                    for viewport in sequences:
                        frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                        if frame:
                            sequences[viewport]=frame['sequence']
                            # Fog depth is deterministic coverage, not stochastic
                            # transparent-shadow dithering of partial-alpha leaves.
                            self.assertAlmostEqual(frame['pixels'][leaf],far[viewport][leaf],delta=4)
                    time.sleep(.002)
                after=runtime.Statistics()
                self.assertEqual(before['fog_material_creations'],after['fog_material_creations'])
                self.assertEqual(before['fog_target_creations'],after['fog_target_creations'])
                # A third surface must keep presenting rather than compete with
                # the other windows for a shared frame scheduler.
                self.assertTrue(runtime.CreateCapturedViewport(509,False,96,80,1),runtime.LastError())
                extra_sequence=0;deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                    for viewport in sequences:
                        frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                        if frame:sequences[viewport]=frame['sequence']
                    frame=runtime.CapturedViewportFrame(509,extra_sequence)
                    if frame:extra_sequence=frame['sequence']
                    if extra_sequence>=4:break
                    time.sleep(.005)
                self.assertGreaterEqual(extra_sequence,4)
                before=runtime.Statistics()
                for _ in range(40):
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                    for viewport in sequences:
                        frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                        if frame:sequences[viewport]=frame['sequence']
                    frame=runtime.CapturedViewportFrame(509,extra_sequence)
                    if frame:extra_sequence=frame['sequence']
                    time.sleep(.005)
                after=runtime.Statistics()
                deltas=[after[f'viewport_{key}_frames']-before[f'viewport_{key}_frames'] for key in (507,508,509)]
                self.assertGreater(min(deltas),0,deltas)
                self.assertLessEqual(max(deltas),min(deltas)*8+16,deltas)
                self.assertEqual(before['fog_material_creations'],after['fog_material_creations'])
                self.assertEqual(before['fog_target_creations'],after['fog_target_creations'])
                runtime.DestroyViewport(509)
                # Differently sized Scene/Game windows must be prepared before
                # rendering, rather than reallocating attachments inside a pass.
                runtime.ResizeViewport(507,160,120,1);runtime.ResizeViewport(508,192,144,1)
                sizes={507:(160,120),508:(192,144)}
                def resized_frames():
                    counts={507:0,508:0};deadline=time.monotonic()+20
                    while time.monotonic()<deadline:
                        self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                        for viewport in sequences:
                            frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                            if frame:
                                sequences[viewport]=frame['sequence']
                                if (frame['width'],frame['height'])==sizes[viewport]:counts[viewport]+=1
                        if all(count>=8 for count in counts.values()):return
                        time.sleep(.005)
                    self.fail('Resized fog viewports stopped presenting')
                resized_frames();before=runtime.Statistics();resized_frames();after=runtime.Statistics()
                self.assertEqual(before['fog_material_creations'],after['fog_material_creations'])
                self.assertEqual(before['fog_target_creations'],after['fog_target_creations'])
                runtime.ResizeViewport(507,128,96,1);runtime.ResizeViewport(508,128,96,1)
                # The temporary coverage pass must not turn alpha holes opaque in color.
                self.assertTrue(runtime.SetSceneEnvironment(scene,{'fog_enabled':False}))
                clear=capture(lambda frames:all(pixels[leaf+2]>150 and pixels[hole]<80 for pixels in frames.values()))
                for pixels in clear.values():self.assertGreater(pixels[leaf+2],150);self.assertLess(pixels[hole+2],80)
            finally:runtime.Release()

    def test_local_fog_without_scene_fog_in_scene_and_game(self):
        import tempfile,uuid
        from pathlib import Path
        app=QApplication.instance() or QApplication([]);runtime=RuntimeService()
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'Assets').mkdir()
            project=root/'Local.bproject'
            project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: "Local"\nAssetDirectory: "Assets"\nStartupScene: ""\nProperties:\n',encoding='utf-8')
            try:
                self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
                self.assertTrue(runtime.LoadProject(project),runtime.LastError())
                self.assertTrue(runtime.SetSceneEnvironment(runtime.SceneInfo()['uuid'],{'fog_enabled':False,'clear_color':(0,0,0,1)}))
                camera=runtime.CreateEntity('Game camera');self.assertTrue(runtime.AddComponent(camera,'Camera'))
                runtime.SetTransform(camera,(0,0,0),(0,0,0,1),(1,1,1));runtime.SetGamePreview(camera)
                runtime.SetComponentProperty(camera,'Camera','Post Processing',False)
                self.assertTrue(runtime.CreateCapturedViewport(511,True,128,96,1))
                self.assertTrue(runtime.CreateCapturedViewport(512,False,128,96,1))
                runtime.SetSceneCamera(511,(0,0,0),(0,0,-1));runtime.SetGrid(False,1)
                runtime.SetEditorIconsVisible(False);runtime.SetEditorOrientationVisible(False)
                owner=runtime.CreateEntity('Fog box');self.assertTrue(runtime.AddComponent(owner,'Local Fog'))
                for name,value in [('Size',(4,4,4)),('Blend Distance',0),('Color',(1,0,0)),('Density',.2),('Steps',64)]:
                    self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog',name,value),name)
                runtime.SetTransform(owner,(0,0,-10),(0,0,0,1),(1,1,1))
                sequences={511:0,512:0}
                def capture():
                    frames={};counts={511:0,512:0};deadline=time.monotonic()+20
                    while time.monotonic()<deadline:
                        self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                        for viewport in sequences:
                            frame=runtime.CapturedViewportFrame(viewport,sequences[viewport])
                            if frame:sequences[viewport]=frame['sequence'];frames[viewport]=frame['pixels'];counts[viewport]+=1
                        if min(counts.values())>=8:return frames
                        time.sleep(.005)
                    self.fail('Local fog viewport stopped presenting')
                runtime.SetComponentEnabled(owner,'Local Fog',False);clear=capture()
                runtime.SetComponentEnabled(owner,'Local Fog',True);fog=capture()
                center=(48*128+64)*4;corner=(4*128+4)*4
                for viewport in sequences:
                    self.assertGreater(fog[viewport][center],clear[viewport][center]+50)
                    self.assertLess(abs(fog[viewport][corner]-clear[viewport][corner]),4)
                bytes64=runtime.Statistics()['fog_texture_bytes']
                self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog','Steps',32));quality32=capture()
                stats32=runtime.Statistics()
                self.assertLess(stats32['fog_texture_bytes'],bytes64)
                # Exact owned-texture accounting for the two 128x96 targets.
                legacy=2*(128*96*12+8*6*128*16)
                self.assertLessEqual(stats32['fog_texture_bytes'],legacy*.71)
                for viewport in sequences:self.assertAlmostEqual(quality32[viewport][center],fog[viewport][center],delta=4)
                self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog','Steps',128));capture()
                self.assertGreater(runtime.Statistics()['fog_texture_bytes'],stats32['fog_texture_bytes'])
                self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog','Steps',33));capture();bucket=runtime.Statistics()
                self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog','Steps',48));capture();same_bucket=runtime.Statistics()
                self.assertEqual(bucket['fog_target_creations'],same_bucket['fog_target_creations'])
                self.assertEqual(bucket['fog_texture_bytes'],same_bucket['fog_texture_bytes'])
                self.assertTrue(runtime.SetComponentProperty(owner,'Local Fog','Steps',32));capture()
                runtime.SetTransform(owner,(30,0,-10),(0,0,0,1),(1,1,1));moved=capture()
                for viewport in sequences:self.assertLess(abs(moved[viewport][center]-clear[viewport][center]),4)
                parent=runtime.CreateEntity('Fog parent');self.assertTrue(runtime.SetParent(owner,parent))
                runtime.SetTransform(parent,(-30,0,0),(0,0,0,1),(1,1,1));inherited=capture()
                for viewport in sequences:self.assertGreater(inherited[viewport][center],clear[viewport][center]+50)
                runtime._host.set_editor_entity_state([owner],[]);hidden=capture()
                self.assertLess(abs(hidden[511][center]-clear[511][center]),4)
                self.assertGreater(hidden[512][center],clear[512][center]+50)
                runtime._host.set_editor_entity_state([],[])
                runtime.SetComponentEnabled(owner,'Local Fog',False);disabled=capture()
                for viewport in sequences:self.assertLess(abs(disabled[viewport][center]-clear[viewport][center]),4)
            finally:runtime.Release()

    def test_orientation_helper_remains_visible_without_channel_zero_clear(self):
        app=QApplication.instance() or QApplication([]);runtime=RuntimeService()
        try:
            self.assertTrue(runtime.ConfigureRenderingBackend('vulkan'))
            self.assertTrue(runtime.CreateCapturedViewport(510,True,96,64,1))
            sequence=0
            def capture():
                nonlocal sequence
                deadline=time.monotonic()+20;count=0
                while time.monotonic()<deadline:
                    self.assertTrue(runtime.Tick(),runtime.LastError());app.processEvents()
                    frame=runtime.CapturedViewportFrame(510,sequence)
                    if frame:
                        sequence=frame['sequence'];count+=1
                        if count>=4:return frame['pixels']
                    time.sleep(.005)
                self.fail('Orientation capture stopped presenting')
            runtime.SetEditorOrientationVisible(True);visible=capture()
            runtime.SetEditorOrientationVisible(False);hidden=capture()
            red=lambda pixels:sum(pixels[i]>150 and pixels[i]>pixels[i+1]+40 and pixels[i]>pixels[i+2]+40 for i in range(0,len(pixels),4))
            self.assertGreater(red(visible),8)
            self.assertEqual(red(hidden),0)
        finally:runtime.Release()
