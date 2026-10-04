from __future__ import annotations
import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import QApplication, QDialog, QMessageBox
from Launcher.catalog import HubCatalog
from Launcher.main import HubWindow
from Launcher.ui import NewProjectDialog

ROOT=Path(__file__).resolve().parents[2]


class MemoryCatalog(HubCatalog):
    def __init__(self,root):
        self.Data={"projects":[],"editors":[{"version":"1.0.0","root":str(root),"command":"Bazzalt.exe","project_format_max":1}],"preferences":{"projects_root":str(root)}}
        self.Saves=0
    def Save(self):self.Saves+=1
    def DiscoverEditors(self,versions=None):self.Discovered=versions


class NativeHubTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App=QApplication.instance() or QApplication([])
        # Windows' offscreen platform does not discover the native font database.
        font=Path("C:/Windows/Fonts/segoeui.ttf")
        if font.is_file():
            identifier=QFontDatabase.addApplicationFont(str(font))
            if identifier>=0:cls.App.setFont(QFont(QFontDatabase.applicationFontFamilies(identifier)[0],9))
    def setUp(self):
        self.Temp=tempfile.TemporaryDirectory(dir=ROOT/"build");self.Root=Path(self.Temp.name);self.Catalog=MemoryCatalog(self.Root)
        self.Project=self.Catalog.CreateProject(self.Root,"Sample Game","1.0.0")
        self.Window=HubWindow(self.Catalog);self.Window.show();self.App.processEvents()
    def tearDown(self):
        self.Window.close();self.Window.deleteLater();self.App.processEvents();self.Temp.cleanup()

    def test_native_hub_has_no_editor_or_webengine_dependencies(self):
        result=subprocess.run([sys.executable,"-c","import sys; import Launcher.main; assert not any(n.startswith(('Editor','PySide6.QtWebEngine','PySide6.QtWebChannel')) for n in sys.modules)"],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(hasattr(self.Window,"View"));self.assertEqual(self.Window.Navigation.count(),3)
        self.assertEqual(self.Window.styleSheet(),"")

    def test_selection_search_and_empty_state(self):
        window=self.Window;self.assertFalse(window.OpenButton.isEnabled());window.SelectProject(self.Project["id"])
        self.assertTrue(window.OpenButton.isEnabled());self.assertEqual(window.LaunchVersion.currentData(),"1.0.0")
        window.Search.setText("absent");self.assertEqual(window.Proxy.rowCount(),0);self.assertEqual(window.ProjectStack.currentIndex(),1);self.assertFalse(window.OpenButton.isEnabled())
        window.Search.setText("sample");self.assertEqual(window.Proxy.rowCount(),1)

    def test_launch_uses_selected_editor_and_keeps_selection(self):
        window=self.Window;window.SelectProject(self.Project["id"])
        with patch("Launcher.main.subprocess.Popen") as popen:
            window.OpenSelected();command=popen.call_args.args[0]
        self.assertEqual(command[0],str(self.Root/"Bazzalt.exe"));self.assertIn(self.Project["path"],command)
        self.assertEqual(window.SelectedProject()["id"],self.Project["id"])

    def test_unavailable_requested_version_is_not_silently_replaced(self):
        with patch.object(self.Window,"ShowError"),patch("Launcher.main.subprocess.Popen") as popen:
            self.assertFalse(self.Window.Bridge.LaunchProject(self.Project["id"],"9.0.0"));popen.assert_not_called()

    def test_new_project_dialog_creates_actual_project_and_selects_it(self):
        dialog=NewProjectDialog(self.Window.Bridge,self.Window)
        self.assertFalse(dialog.CreateButton.isEnabled());dialog.Name.setText("New Game");self.assertTrue(dialog.CreateButton.isEnabled())
        dialog.Create();self.assertEqual(dialog.result(),QDialog.DialogCode.Accepted)
        self.assertTrue((self.Root/"New Game/New Game.bproject").is_file());self.assertEqual(self.Window.SelectedProject()["name"],"New Game")

    def test_failed_creation_keeps_dialog_open(self):
        dialog=NewProjectDialog(self.Window.Bridge,self.Window);dialog.Name.setText("Sample Game")
        with patch.object(self.Window,"ShowError"):dialog.Create()
        self.assertEqual(dialog.result(),QDialog.DialogCode.Rejected)

    def test_unicode_project_creation(self):
        dialog=NewProjectDialog(self.Window.Bridge,self.Window);dialog.Name.setText("Игра 空");dialog.Create()
        self.assertEqual(dialog.result(),QDialog.DialogCode.Accepted);self.assertTrue((self.Root/"Игра 空/Игра 空.bproject").is_file())

    def test_remove_only_removes_registration(self):
        with patch("Launcher.ui.QMessageBox.question",return_value=QMessageBox.StandardButton.Yes):self.Window.RemoveProject(self.Project)
        self.assertFalse(self.Catalog.Data["projects"]);self.assertTrue(Path(self.Project["path"]).is_file())

    def test_missing_project_and_incompatible_editor_disable_open(self):
        window=self.Window;Path(self.Project["path"]).unlink();window.Refresh();window.SelectProject(self.Project["id"]);self.assertFalse(window.OpenButton.isEnabled())
        with patch.object(window,"ShowError"),patch("Launcher.main.subprocess.Popen") as popen:
            self.assertFalse(window.Bridge.LaunchProject(self.Project["id"],"1.0.0"));popen.assert_not_called()

    def test_preferences_and_window_geometry_are_saved(self):
        window=self.Window;window.ProjectRoot.setText(str(self.Root/"Projects"));window.SaveProjectRoot();window.ConfirmRemove.setChecked(False);window.close()
        preferences=self.Catalog.Data["preferences"]
        self.assertEqual(preferences["projects_root"],str(self.Root/"Projects"));self.assertFalse(preferences["confirm_remove"]);self.assertTrue(preferences["window_geometry"])

    def test_navigation_and_native_preview(self):
        window=self.Window;window.Navigation.setCurrentRow(1);self.assertEqual(window.Pages.currentIndex(),1)
        window.Navigation.setCurrentRow(2);self.assertEqual(window.Pages.currentIndex(),2)
        window.Navigation.setCurrentRow(0);window.SelectProject(self.Project["id"]);self.App.processEvents()
        self.assertTrue(window.grab().save(str(ROOT/"build/hub-native-preview.png")))


if __name__=="__main__":unittest.main()
