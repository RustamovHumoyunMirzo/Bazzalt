"""Cocoa integration checks; run on a real macOS desktop session in CI."""
import os
import sys
import unittest
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication,QMainWindow,QWidget
from PySide6.QtGui import QAction
from Editor.theme import ThemeManager
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from Editor.gui.widgets.menu_bar import EditorMenuBar
from Editor.runtime import RuntimeService

@unittest.skipUnless(sys.platform=='darwin' and os.environ.get('QT_QPA_PLATFORM')=='cocoa','Native macOS session required')
class MacNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_native_menu_without_duplicate_client_strip(self):
        window=QMainWindow();themes=ThemeManager(self.App);menu=EditorMenuBar(themes,LocalizationManager(ResourceManager()),window);window.setMenuBar(menu);window.resize(800,600);window.show();self.App.processEvents()
        try:
            self.assertTrue(menu.isNativeMenuBar());self.assertFalse(menu.BrandLabel.isVisible());self.assertEqual(menu.PreferencesAction.menuRole(),QAction.MenuRole.PreferencesRole);self.assertEqual(menu.ExitAction.menuRole(),QAction.MenuRole.QuitRole);self.assertGreater(len(menu.actions()),5)
        finally:window.close();window.deleteLater();self.App.processEvents()
    @unittest.skipUnless(os.environ.get("BAZZALT_MAC_GPU_TEST")=="1","A Metal-capable Mac is required for GPU presentation")
    def test_metal_viewport_attach_resize_and_release(self):
        runtime=RuntimeService();self.assertTrue(runtime.IsAvailable(),runtime.LastError());self.assertIn('metal',runtime.SupportedRenderingBackends());self.assertTrue(runtime.ConfigureRenderingBackend('metal'))
        surface=QWidget();surface.setAttribute(Qt.WidgetAttribute.WA_NativeWindow);surface.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen);surface.resize(480,320);surface.show();self.App.processEvents();ratio=surface.devicePixelRatioF()
        try:
            self.assertTrue(runtime.CreateViewport(991,int(surface.winId()),True,int(surface.width()*ratio),int(surface.height()*ratio),ratio),runtime.LastError());runtime.Tick();surface.resize(640,420);self.App.processEvents();runtime.ResizeViewport(991,int(640*ratio),int(420*ratio),ratio);runtime.Tick()
        finally:runtime.DestroyViewport(991);runtime.Release();surface.close();surface.deleteLater();self.App.processEvents()
