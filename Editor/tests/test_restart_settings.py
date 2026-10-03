import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch
from PySide6.QtWidgets import QApplication, QWidget
from Editor.gui.application import Editor
from Editor.gui.preferences import PreferencesDialog, MergePreferences
from Editor.asset_opening import ExternalAssetOpener
from Editor.main import RestartCommand


class RestartSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_backend_choices_follow_native_build_and_roundtrip(self):
        parent = QWidget()
        parent.Localization = SimpleNamespace(Translate=lambda key: key)
        parent.Runtime = SimpleNamespace(SupportedRenderingBackends=lambda: ["automatic", "vulkan", "opengl"])
        parent.GetPreferences = lambda: MergePreferences({"rendering": {"backend": "vulkan"}})
        parent.ApplyPreferences = Mock()
        parent._settings = {}
        parent.AssetBrowser = SimpleNamespace(ExternalOpener=ExternalAssetOpener(parent._settings))
        dialog = PreferencesDialog(parent)
        field = dialog.Controls["rendering_backend"]
        self.assertEqual([field.itemData(i) for i in range(field.count())], ["automatic", "vulkan", "opengl"])
        self.assertEqual(field.currentData(), "vulkan")
        self.assertEqual(dialog.Values()["rendering"]["backend"], "vulkan")
        parent.close()

    def test_critical_preferences_prompt_only_after_accepted_change(self):
        editor = Mock()
        editor.Localization.Translate = lambda key: key
        editor.PreferenceValue.side_effect = ["automatic", "vulkan"]
        with patch("Editor.gui.application.PreferencesDialog") as dialog:
            dialog.return_value.exec.return_value = 1
            Editor.OpenPreferences(editor)
        editor.RequestRestart.assert_called_once_with()
        editor.RequestRestart.reset_mock()
        with patch("Editor.gui.application.PreferencesDialog") as dialog:
            editor.PreferenceValue.side_effect = ["automatic"]
            dialog.return_value.exec.return_value = 0
            Editor.OpenPreferences(editor)
        editor.RequestRestart.assert_not_called()

    def test_project_rename_requests_restart(self):
        editor = Mock()
        editor.Localization.Translate = lambda key: key
        editor.Runtime.ProjectInfo.side_effect = [{"name": "Old"}, {"name": "New"}]
        with patch("Editor.gui.project_settings.ProjectSettingsDialog") as dialog:
            dialog.return_value.exec.return_value = 1
            Editor.OpenProjectSettings(editor)
        editor.RequestRestart.assert_called_once_with()

    def test_restart_respects_later_and_cancelled_close(self):
        editor = Mock()
        editor.Localization.Translate = lambda key: key
        for now, can_close in ((False, True), (True, False), (True, True)):
            with self.subTest(now=now, can_close=can_close), patch("Editor.gui.application.QMessageBox") as message, patch("Editor.gui.application.QApplication") as app:
                buttons = [object(), object()]
                message.return_value.addButton.side_effect = buttons
                message.return_value.clickedButton.return_value = buttons[0 if now else 1]
                editor.close.return_value = can_close
                Editor.RequestRestart(editor)
                message.return_value.setInformativeText.assert_not_called()
                if now and can_close:
                    app.instance.return_value.exit.assert_called_once_with(75)
                else:
                    app.instance.return_value.exit.assert_not_called()

    def test_restart_command_preserves_hub_arguments(self):
        project = Path("C:/Документы/Game/Test.bproject")
        with patch("Editor.main.sys.frozen", False, create=True):
            command = RestartCommand(project, "1.2.3")
            self.assertEqual(command[1:3], ["-m", "Editor.main"])
        with patch("Editor.main.sys.frozen", True, create=True):
            command = RestartCommand(project, "1.2.3")
            self.assertNotIn("-m", command)
        self.assertEqual(command[-4:], ["--project", str(project), "--editor-version", "1.2.3"])
