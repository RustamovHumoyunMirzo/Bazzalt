import os
import time
import unittest
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor

@unittest.skipUnless(os.environ.get('QT_QPA_PLATFORM','').startswith('wayland'),'Native Wayland compositor required')
class WaylandNativeTests(unittest.TestCase):
    def test_scene_presents_and_resizes_without_x11_window_handles(self):
        app=QApplication.instance() or QApplication([]);window=Editor();window.resize(1000,700);window.show()
        try:
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                app.processEvents();window.Runtime.Tick();surface=window.Scene.Surface
                if surface._attached and not surface._frame_image.isNull():break
                time.sleep(.01)
            self.assertTrue(surface._captured);self.assertTrue(surface._attached,window.Runtime.LastError());self.assertFalse(surface._frame_image.isNull())
            old=surface._frame_sequence;window.resize(1200,800);deadline=time.monotonic()+15
            while surface._frame_sequence<=old and time.monotonic()<deadline:app.processEvents();window.Runtime.Tick();time.sleep(.01)
            self.assertGreater(surface._frame_sequence,old)
        finally:window.close();window.deleteLater();app.processEvents()
