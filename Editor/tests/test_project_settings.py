"""Project settings do not reload scenes or discard extensible metadata."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication, QWidget
from Editor.gui.project_settings import ProjectSettingsDialog


class ProjectSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def test_apply_preserves_metadata(self):
        parent = QWidget()
        parent.Localization = SimpleNamespace(Translate=lambda key: key)
        parent.Runtime = Mock()
        parent.Runtime.ProjectInfo.return_value = {
            "name": "Example", "properties": {"custom.key": "keep"}}
        parent.Runtime.UpdateProjectInfo.return_value = True
        dialog = ProjectSettingsDialog(parent)
        dialog.Name.setText("  Renamed  ")
        dialog.Fields["company"].setText("Studio")
        dialog._Apply()
        name, properties = parent.Runtime.UpdateProjectInfo.call_args.args
        self.assertEqual(name, "Renamed")
        self.assertEqual(properties["custom.key"], "keep")
        self.assertEqual(properties["project.company"], "Studio")
        self.assertEqual(dialog.result(), dialog.DialogCode.Accepted)
        parent.close()

    def test_empty_name_cannot_apply(self):
        parent = QWidget()
        parent.Localization = SimpleNamespace(Translate=lambda key: key)
        parent.Runtime = Mock()
        parent.Runtime.ProjectInfo.return_value = {"name": "Example"}
        dialog = ProjectSettingsDialog(parent)
        dialog.Name.setText(" ")
        self.assertFalse(dialog.Apply.isEnabled())
        parent.close()
