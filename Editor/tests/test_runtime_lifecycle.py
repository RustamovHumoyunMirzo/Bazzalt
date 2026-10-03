"""Offscreen plugins must not be submitted as OS presentation handles."""
import os
import unittest
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
from PySide6.QtCore import QEvent, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication
from Editor.gui.application import Editor
from Editor.gui.panels.viewport import NativeRenderSurface
from Editor.theme import ThemeManager

class RuntimeLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])

    def test_offscreen_surface_never_creates_native_swapchain(self):
        runtime=Mock();runtime.IsAvailable.return_value=True
        surface=NativeRenderSurface(runtime,True);surface.show()
        try:
            QTest.qWait(100);surface._Attach()
            runtime.CreateViewport.assert_not_called();self.assertFalse(surface._attached)
        finally:surface._fly_timer.stop();surface.close();surface.deleteLater()

    def test_close_releases_runtime_and_does_not_restart_delayed_ticks(self):
        window=Editor(ThemeManager(self.App))
        window.Controller.Timer.start();self.assertTrue(window.Controller.Timer.isActive())
        QTimer.singleShot(0,window.Controller._StartTicking)
        window.close();QTest.qWait(150)
        self.assertFalse(window.Runtime.IsAvailable());self.assertFalse(window.Controller.Timer.isActive())
        self.assertFalse(window.Scene.Surface._fly_timer.isActive());self.assertFalse(window.Output.Surface._fly_timer.isActive())
        window.deleteLater();self.App.processEvents()

    def test_deleted_editor_cancels_deferred_widget_callbacks(self):
        with patch("sys.excepthook") as errors:
            window=Editor(ThemeManager(self.App))
            window.show();window.close();window.deleteLater()
            self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
            QTest.qWait(150)
            errors.assert_not_called()

if __name__=="__main__":unittest.main()
