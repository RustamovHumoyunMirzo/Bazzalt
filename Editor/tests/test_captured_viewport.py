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
