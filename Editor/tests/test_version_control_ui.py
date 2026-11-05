import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
import shutil
import subprocess
import tempfile
from time import monotonic
import unittest
import uuid
import yaml

from PySide6.QtCore import QEvent, Qt
from PySide6.QtTest import QTest, QSignalSpy
from PySide6.QtWidgets import QApplication, QPushButton

from Editor.gui.application import Editor
from Editor.gui.version_control import VersionControlDialog
from Editor.version_control import GitRepository


@unittest.skipUnless(shutil.which("git"), "Git executable required")
class VersionControlUITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.App = QApplication.instance() or QApplication([])

    def Wait(self, dialog):
        deadline = monotonic() + 20
        while dialog.Busy and monotonic() < deadline:
            QTest.qWait(20)
        self.assertFalse(dialog.Busy, dialog.Status.toPlainText())

    def Git(self, root, *args):
        result = subprocess.run(["git", *args], cwd=root, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))

    def test_menu_stage_commit_and_native_worktree_reload(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            assets = root / "Assets"
            assets.mkdir()
            project = root / "Test.bproject"
            project.write_text(f'FormatVersion: 1\nProjectUUID: "{uuid.uuid4()}"\nName: Test\nAssetDirectory: Assets\nStartupScene: ""\nProperties: {{}}\n', encoding="utf-8")
            editor = Editor(settings={}, settings_saver=lambda _value: None)
            dialog = None
            try:
                if not editor.Runtime.IsAvailable():
                    self.skipTest("Native editor bridge unavailable")
                self.assertTrue(editor.Runtime.LoadProject(project), editor.Runtime.LastError())
                editor.Controller.Timer.stop()
                entity = editor.Runtime.CreateEntity("Before")
                scene = assets / "Main.bscene"
                self.assertTrue(editor.Runtime.SaveScene(scene))
                settings = yaml.safe_load(project.read_text(encoding="utf-8"))
                settings["StartupScene"] = "Assets/Main.bscene"
                project.write_text(yaml.safe_dump(settings), encoding="utf-8")
                self.assertTrue(editor.Runtime.LoadProject(project))
                editor.Controller.SetDirty(False)
                repository = GitRepository.Initialize(root)
                self.Git(root, "config", "user.name", "Editor Tests")
                self.Git(root, "config", "user.email", "editor@example.invalid")
                self.Git(root, "config", "commit.gpgSign", "false")
                self.Git(root, "config", "core.hooksPath", str(root / ".git/hooks"))
                excludes = root / ".git/test-excludes"
                excludes.write_text("", encoding="utf-8")
                self.Git(root, "config", "core.excludesFile", str(excludes))
                self.Git(root, "config", "core.autocrlf", "false")
                repository.Stage([change.Path for change in repository.Snapshot().Changes])
                repository.Commit("Initial")
                main = repository.Snapshot().Branch
                repository.Switch("feature", True)
                self.assertTrue(editor.Runtime.Rename(entity, "After"))
                self.assertTrue(editor.Runtime.SaveScene(scene))
                editor.Controller.SetDirty(False)

                spy = QSignalSpy(editor.MenuBar.VersionControlRequested)
                editor.MenuBar._RefreshWindowMenu()
                action = next(action for action in editor.MenuBar.WindowMenu.actions() if action.text() == editor.Localization.Translate("vcs.title"))
                action.trigger()
                self.assertEqual(spy.count(), 1)
                dialog = editor._version_control_dialog
                dialog.AutoRefresh.stop()
                self.Wait(dialog)
                self.assertEqual(dialog.State.Branch, "feature")
                item = next(dialog.Changes.topLevelItem(index) for index in range(dialog.Changes.topLevelItemCount())
                            if dialog.Changes.topLevelItem(index).data(0, Qt.ItemDataRole.UserRole) == "Assets/Main.bscene")
                item.setSelected(True)
                self.Wait(dialog)
                stage = next(button for button in dialog.findChildren(QPushButton) if button.text() == editor.Localization.Translate("vcs.stage"))
                stage.click()
                self.Wait(dialog)
                self.assertTrue(any(change.Path == "Assets/Main.bscene" and change.Index == "M" for change in dialog.State.Changes))
                dialog.Message.setPlainText("Feature from editor")
                commit = next(button for button in dialog.findChildren(QPushButton) if button.text() == editor.Localization.Translate("vcs.commit"))
                commit.click()
                self.Wait(dialog)
                self.assertEqual(dialog.State.History[0][3], "Feature from editor")
                self.assertEqual(dialog.Message.toPlainText(), "")

                # This writes a genuinely different scene revision while its
                # native editor lock is held. The dialog must release the lock,
                # reload the scene, and discard the previous revision's undo state.
                dialog.Mutate("Switch", main, worktree=True)
                self.Wait(dialog)
                self.assertFalse(dialog.ReloadFailed, dialog.Status.toPlainText())
                self.assertEqual(dialog.State.Branch, main, dialog.Status.toPlainText())
                self.assertEqual(editor.Runtime.EntityDetails(entity)["name"], "Before")
                self.assertIsNone(dialog.PendingUpdate)
                self.assertIsNone(editor.ShaderGraph.Path)
                self.assertIsNone(editor.Animation.Path)

                editor.Controller.SetDirty(True)
                dialog.Mutate("Switch", "feature", worktree=True)
                self.assertFalse(dialog.Busy)
                self.assertIn(editor.Localization.Translate("vcs.save_stop_first"), dialog.Status.toPlainText())
                self.assertEqual(repository.Snapshot().Branch, main)
                editor.Controller.SetDirty(False)

                self.assertTrue(editor.Runtime.Rename(entity, "Main Edit"))
                self.assertTrue(editor.Runtime.SaveScene(scene))
                repository.Stage(["Assets/Main.bscene"])
                repository.Commit("Different main edit")
                dialog.Mutate("Merge", "feature", worktree=True)
                self.Wait(dialog)
                self.assertTrue(dialog.State.Merge, dialog.Status.toPlainText())
                self.assertTrue(any(change.Conflict for change in dialog.State.Changes))
                self.assertIsNotNone(dialog.PendingUpdate)
                self.assertFalse(dialog.ReloadFailed, dialog.Status.toPlainText())
                dialog.Mutate("ResolveSide", "Assets/Main.bscene", 3, worktree=True)
                self.Wait(dialog)
                # Imported scene content also changes its metadata fingerprint.
                # Resolve every metadata conflict before the runtime can resume.
                remaining = [change.Path for change in dialog.State.Changes if change.Conflict]
                if remaining:
                    self.assertIsNotNone(dialog.PendingUpdate)
                for path in remaining:
                    dialog.Mutate("ResolveSide", path, 3, worktree=True)
                    self.Wait(dialog)
                self.assertIsNone(dialog.PendingUpdate, dialog.Status.toPlainText())
                self.assertFalse(dialog.ReloadFailed, dialog.Status.toPlainText())
                self.assertEqual(editor.Runtime.EntityDetails(entity)["name"], "After")
                dialog.Message.setPlainText("Merge resolved in editor")
                commit.click()
                self.Wait(dialog)
                self.assertFalse(dialog.State.Merge, dialog.Status.toPlainText())
            finally:
                if dialog is not None:
                    self.Wait(dialog)
                    dialog.Shutdown()
                editor.Controller.SetDirty(False)
                editor.close()
                editor.deleteLater()
                self.App.sendPostedEvents(None, QEvent.Type.DeferredDelete)
