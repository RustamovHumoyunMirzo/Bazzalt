import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import sys
import unittest
from types import SimpleNamespace
from copy import deepcopy
from unittest.mock import Mock,patch
from PySide6.QtWidgets import QApplication,QWidget
from PySide6.QtCore import QEvent
from Editor.asset_opening import ExternalAssetOpener
from Editor.gui.preferences import PreferencesDialog,MergePreferences
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager


class PreferencesEditor(QWidget):
    def __init__(self):
        super().__init__();self.Localization=LocalizationManager(ResourceManager())
        self._settings={"preferences":MergePreferences(None),"external_asset_applications":{
            ".cpp":{"platform":sys.platform,"application":"C:/Tools/Code.exe"}}}
        self.Save=Mock();self.Runtime=SimpleNamespace(SupportedRenderingBackends=lambda:["automatic"])
        self.AssetBrowser=SimpleNamespace(ExternalOpener=ExternalAssetOpener(self._settings,self.Save))
    def GetPreferences(self):return MergePreferences(self._settings["preferences"])
    def ApplyPreferences(self,value):self._settings["preferences"]=value;self.Save(self._settings)


class FileAssociationPreferencesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):self.Editor=PreferencesEditor();self.Dialog=PreferencesDialog(self.Editor)
    def tearDown(self):
        self.Dialog.close();self.Editor.close();self.Editor.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_section_lists_choices_and_reset_cancel_changes_nothing(self):
        self.assertEqual(self.Dialog.Sections.item(7).text(),"File Associations")
        self.assertEqual(self.Dialog.AssociationList.topLevelItem(0).text(0),".cpp")
        before=deepcopy(self.Editor._settings)
        self.Dialog.AssociationList.setCurrentItem(self.Dialog.AssociationList.topLevelItem(0))
        self.Dialog.AssociationReset.click();self.Dialog.reject()
        self.assertEqual(self.Editor._settings,before);self.Editor.Save.assert_not_called()
    def test_apply_resets_saved_choices_and_persists_remember_setting(self):
        self.Dialog.AssociationResetAll.click();self.Dialog.Controls["remember_associations"].setChecked(False)
        self.Dialog._Apply()
        self.assertEqual(self.Editor.AssetBrowser.ExternalOpener.Associations(),{})
        self.assertFalse(self.Editor.GetPreferences()["file_associations"]["remember"])
        self.Editor.Save.assert_called_once_with(self.Editor._settings)
    def test_explicit_application_configuration_is_staged_until_apply(self):
        self.Dialog.AssociationList.setCurrentItem(self.Dialog.AssociationList.topLevelItem(0))
        with patch("Editor.gui.preferences.ChooseApplication",return_value="C:/Tools/New.exe") as choose:self.Dialog.AssociationChoose.click()
        choose.assert_called_once();self.assertEqual(self.Editor.AssetBrowser.ExternalOpener.Associations()[".cpp"]["application"],"C:/Tools/Code.exe")
        self.Dialog._Apply();self.assertEqual(self.Editor.AssetBrowser.ExternalOpener.Associations()[".cpp"]["application"],"C:/Tools/New.exe")
    def test_restore_defaults_clears_associations_only_on_apply(self):
        self.Dialog.Restore.click();self.assertEqual(self.Dialog._associations,{})
        self.assertTrue(self.Editor.AssetBrowser.ExternalOpener.Associations())
        self.Dialog._Apply();self.assertFalse(self.Editor.AssetBrowser.ExternalOpener.Associations())

if __name__=="__main__":unittest.main()
