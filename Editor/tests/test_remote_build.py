import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from Editor.remote_build import DEFAULT,Command,Package

class RemoteBuildTests(unittest.TestCase):
    def test_credentials_are_not_in_profiles_and_command_is_quoted(self):
        self.assertNotIn('password',DEFAULT);self.assertNotIn('passphrase',DEFAULT)
        profile=DEFAULT|dict(compiler='/tools/compiler path',worker_platform='linux');command=Command(profile,['--remote-request',"/tmp/project'; touch bad"])
        import shlex
        self.assertEqual(shlex.split(command),[profile['compiler'],'--remote-request',"/tmp/project'; touch bad"])
    def test_sync_excludes_caches_and_credentials(self):
        import zipfile
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'project';root.mkdir();project=root/'Game.bproject';project.write_text('AssetDirectory: Assets\n');(root/'Assets').mkdir();(root/'Assets/cube.cpp').write_text('code');(root/'.env').write_text('secret');(root/'private.pem').write_text('key');(root/'bin').mkdir();(root/'bin/game.exe').write_bytes(b'output')
            (root/'Assets/bin').mkdir();(root/'Assets/bin/data.txt').write_text('asset')
            archive=Path(directory)/'project.zip';Package(project,archive,DEFAULT)
            with zipfile.ZipFile(archive) as source:self.assertEqual(set(source.namelist()),{'Game.bproject','Assets/cube.cpp','Assets/bin/data.txt'})
    def test_reject_policy_is_used_for_unknown_ssh_hosts(self):
        from Editor.remote_build import Connect
        import sys
        from types import SimpleNamespace
        from unittest.mock import Mock
        module=Mock();client=module.SSHClient.return_value
        with patch.dict(sys.modules,{'paramiko':module}):
            self.assertIs(Connect(DEFAULT|dict(host='localhost',user='builder')),client)
            client.set_missing_host_key_policy.assert_called_once_with(module.RejectPolicy.return_value);module.AutoAddPolicy.assert_not_called()

    def test_modal_worker_profiles_never_persist_credentials(self):
        import os
        os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
        from PySide6.QtWidgets import QApplication,QWidget
        from Editor.gui.remote_build import RemoteBuildDialog
        from Editor.localization import LocalizationManager
        from Editor.resources import ResourceManager
        from bazzalt.settings import DataPaths
        from unittest.mock import Mock
        app=QApplication.instance() or QApplication([])
        with tempfile.TemporaryDirectory() as directory,patch.object(DataPaths,'Root',return_value=Path(directory)):
            editor=QWidget();editor.Localization=LocalizationManager(ResourceManager());editor.Runtime=Mock();dialog=RemoteBuildDialog(editor)
            try:
                dialog.Fields['host'].setText('example.invalid');dialog.Fields['user'].setText('builder');dialog.Credential.setText('never-store-me');self.assertTrue(dialog.Save());self.assertTrue(dialog.isModal())
                self.assertNotIn('never-store-me',(Path(directory)/'Config/remote-builds.json').read_text())
                field=dialog.Fields['authentication'];field.setCurrentIndex(field.findData('password'));self.assertTrue(dialog.Fields['private_key'].parentWidget().isHidden())
            finally:dialog.deleteLater();editor.deleteLater();app.processEvents()
