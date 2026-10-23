import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from types import SimpleNamespace
import tempfile,unittest
from unittest.mock import Mock,patch
from PySide6.QtWidgets import QApplication,QWidget
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.build_settings import BuildSettingsDialog
from Compiler.settings import Load

class GameBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_modal_settings_and_preset_persistence(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'Game.bproject';editor=QWidget();editor.Runtime=Mock();editor.Runtime.ProjectInfo.return_value={'project_path':str(project)};editor.Localization=LocalizationManager(ResourceManager())
            dialog=BuildSettingsDialog(editor);self.assertTrue(dialog.isModal());self.assertEqual(dialog.Sections.count(),6)
            preset=dialog.Controls[('security','preset')];preset.setCurrentIndex(preset.findData('high'));dialog._Preset();self.assertEqual(dialog.GetValues()['security']['key_storage'],'environment')
            dialog._Apply();self.assertEqual(Load(project)['security']['preset'],'high');dialog.deleteLater();editor.deleteLater()
    def Controller(self,launch=False):
        from Editor.game_build import GameBuildController
        editor=QWidget();editor.MenuBar=Mock();editor.Console=Mock();editor.Localization=LocalizationManager(ResourceManager());controller=GameBuildController(editor)
        controller.Process=Mock();controller.Process.readAllStandardOutput.return_value=b'';controller.Process.readAllStandardError.return_value=b''
        controller.Progress=Mock();controller._stderr='';controller._error='';controller._result_path='C:/Документы/Game';controller._launch_requested=launch;controller._launch_error=''
        self.addCleanup(editor.deleteLater);return editor,controller
    def test_build_only_completion_dialog_and_launch_only_console(self):
        from PySide6.QtCore import QProcess
        editor,controller=self.Controller()
        with patch('Editor.game_build.BuildCompleteDialog') as complete:
            controller._Finished(0,QProcess.ExitStatus.NormalExit);complete.assert_called_once();complete.return_value.show.assert_called_once();editor.Console.AddMessage.assert_not_called()
        editor,controller=self.Controller(True)
        with patch('Editor.game_build.BuildCompleteDialog') as complete:
            controller._Finished(0,QProcess.ExitStatus.NormalExit);complete.assert_not_called();self.assertIn('Документы',editor.Console.AddMessage.call_args.args[0])
    def test_failure_never_opens_completion_and_progress_blocks_close(self):
        from PySide6.QtCore import QProcess
        from Editor.gui.build_progress import BuildProgressDialog
        editor,controller=self.Controller();controller._error='A real failure'
        with patch('Editor.game_build.BuildCompleteDialog') as complete:
            controller._Finished(1,QProcess.ExitStatus.NormalExit);complete.assert_not_called();controller.Progress.Fail.assert_called_once_with('A real failure')
        dialog=BuildProgressDialog(editor);dialog.show();dialog.close();self.assertTrue(dialog.isVisible());self.assertTrue(dialog.isModal());dialog.Finish();self.assertFalse(dialog.isVisible());dialog.deleteLater()
