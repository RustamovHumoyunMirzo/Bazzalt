import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import Mock, patch
import unittest

from PySide6.QtCore import QEvent, QPoint, Qt, QTimer
from PySide6.QtWidgets import QApplication, QFileDialog
from Editor.asset_opening import ExternalAssetOpener
from Editor.platform_services import ChooseApplication, LaunchApplication, NativeOpenResult, OpenWithApplication
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager
from bazzalt.settings import SettingsStore


class AssetOpeningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App = QApplication.instance() or QApplication([])

    def setUp(self):
        # Generic association tests do not display any real native app chooser.
        platform = patch("Editor.asset_opening.sys", Mock(platform="linux"))
        platform.start();self.addCleanup(platform.stop)
        self.Temp = TemporaryDirectory()
        self.Root = Path(self.Temp.name)
        self.Files = [self.Root / "Player.cpp", self.Root / "Other.CPP"]
        for path in self.Files:path.write_text("// test", encoding="utf-8")
        self.Settings = {"schema_version": 3, "preferences": {"keep": True}}
        self.Save = Mock()
        self.Opener = ExternalAssetOpener(self.Settings, self.Save)
        self.Locale = LocalizationManager(ResourceManager())

    def tearDown(self):self.Temp.cleanup()

    def test_selection_remembered_per_extension_across_sessions(self):
        with patch("Editor.asset_opening.ChooseApplication", return_value="C:/Tools/Code.exe") as choose, patch("Editor.asset_opening.LaunchApplication") as launch, patch("Editor.asset_opening.ValidateApplication"):
            self.Opener.Open(self.Files, None, self.Locale)
            choose.assert_called_once()
            launch.assert_called_once_with("C:/Tools/Code.exe", [path.resolve() for path in self.Files])
            self.Save.assert_called_once_with(self.Settings)
            ExternalAssetOpener(self.Settings).Open([self.Files[1]], None, self.Locale)
            self.assertEqual(choose.call_count, 1)
            self.assertEqual(launch.call_count, 2)
            self.assertEqual(self.Settings["preferences"], {"keep": True})

    def test_association_survives_settings_file_roundtrip(self):
        store = SettingsStore("editor", 3, lambda: {"schema_version": 3})
        store.Path = self.Root / "editor.json"
        with patch("Editor.asset_opening.ChooseApplication", return_value="chosen") as choose, patch("Editor.asset_opening.LaunchApplication"), patch("Editor.asset_opening.ValidateApplication"):
            ExternalAssetOpener(self.Settings, store.Save).Open(self.Files, None, self.Locale)
            loaded = store.Load()
            self.assertEqual(loaded[self.Opener.SettingsKey], self.Settings[self.Opener.SettingsKey])
            ExternalAssetOpener(loaded, store.Save).Open(self.Files, None, self.Locale)
            self.assertEqual(choose.call_count, 1)

    def test_windows_native_chooser_launches_first_once_and_remembers_exposed_app(self):
        with patch("Editor.asset_opening.sys", Mock(platform="win32")), patch("Editor.asset_opening.OpenWithApplication", return_value=NativeOpenResult(True, "chosen")) as native, patch("Editor.asset_opening.LaunchApplication") as launch, patch("Editor.asset_opening.ValidateApplication"), patch("Editor.asset_opening.ChooseApplication") as browse:
            self.Opener.Open(self.Files, None, self.Locale)
            native.assert_called_once_with(self.Files[0].resolve(), None)
            launch.assert_called_once_with("chosen", [self.Files[1].resolve()])
            self.Opener.Open([self.Files[0]], None, self.Locale)
            self.assertEqual(native.call_count, 1)
            browse.assert_not_called()

    def test_windows_cancel_keeps_choice_and_unknown_replacement_invalidates_it(self):
        self.Settings[self.Opener.SettingsKey] = {".cpp": {"platform": "win32", "application": "old"}}
        with patch("Editor.asset_opening.sys", Mock(platform="win32")), patch("Editor.asset_opening.OpenWithApplication", side_effect=[NativeOpenResult(False), NativeOpenResult(True)]) as native, patch("Editor.asset_opening.LaunchApplication") as launch:
            self.Opener.Open([self.Files[0]], None, self.Locale, choose=True)
            self.assertEqual(self.Settings[self.Opener.SettingsKey][".cpp"]["application"], "old")
            self.Opener.Open([self.Files[0]], None, self.Locale, choose=True)
            self.assertNotIn(".cpp", self.Settings[self.Opener.SettingsKey])
            launch.assert_not_called()

    def test_windows_shell_api_flags_unicode_cancellation_and_com_cleanup(self):
        import ctypes
        shell = Mock();ole = Mock();ole.CoInitializeEx.return_value = 0
        shell.SHOpenWithDialog.return_value = 0
        path = self.Root / "Код.cpp";path.write_text("test", encoding="utf-8")
        with patch.object(ctypes, "WinDLL", side_effect=[shell, ole], create=True), patch("Editor.platform_services._WindowsOpenWithRecent", side_effect=[None, "code.exe"]), patch("Editor.platform_services._WindowsApplicationPath", return_value="chosen"):
            result = OpenWithApplication(path)
            self.assertEqual(result, NativeOpenResult(True, "chosen"))
            info = shell.SHOpenWithDialog.call_args.args[1]._obj
            self.assertEqual(info.pcszFile, str(path.resolve()))
            self.assertEqual(info.oaifInFlags, 4)
            ole.CoUninitialize.assert_called_once()
        shell.SHOpenWithDialog.return_value = -2147023673  # HRESULT_FROM_WIN32(ERROR_CANCELLED)
        ole.CoUninitialize.reset_mock()
        with patch.object(ctypes, "WinDLL", side_effect=[shell, ole], create=True), patch("Editor.platform_services._WindowsOpenWithRecent", return_value=None):
            self.assertFalse(OpenWithApplication(path).Opened)
            ole.CoUninitialize.assert_called_once()

    def test_windows_stale_history_is_never_cached_as_selected_application(self):
        import ctypes
        shell = Mock();ole = Mock();ole.CoInitializeEx.return_value = 0
        shell.SHOpenWithDialog.return_value = 0
        with patch.object(ctypes, "WinDLL", side_effect=[shell, ole], create=True), patch("Editor.platform_services._WindowsOpenWithRecent", return_value="old.exe"), patch("Editor.platform_services._WindowsApplicationPath") as resolve:
            self.assertEqual(OpenWithApplication(self.Files[0]), NativeOpenResult(True))
            resolve.assert_not_called()

    def test_windows_repeat_most_recent_app_is_remembered_when_history_updated(self):
        import ctypes
        shell=Mock();ole=Mock();ole.CoInitializeEx.return_value=0;shell.SHOpenWithDialog.return_value=0
        with patch.object(ctypes,"WinDLL",side_effect=[shell,ole],create=True),patch("Editor.platform_services._WindowsOpenWithRecent",side_effect=[("code.exe",100),("code.exe",200)]),patch("Editor.platform_services._WindowsApplicationPath",return_value="chosen") as resolve:
            self.assertEqual(OpenWithApplication(self.Files[0]),NativeOpenResult(True,"chosen"))
            resolve.assert_called_once_with("code.exe")

    def test_disabling_remember_always_asks_and_does_not_overwrite_saved_choice(self):
        self.Settings["preferences"]["file_associations"]={"remember":False}
        self.Settings[self.Opener.SettingsKey]={".cpp":{"platform":"linux","application":"old"}}
        with patch("Editor.asset_opening.ChooseApplication",return_value="new") as choose,patch("Editor.asset_opening.LaunchApplication"):
            self.Opener.Open(self.Files,None,self.Locale);self.Opener.Open(self.Files,None,self.Locale)
            self.assertEqual(choose.call_count,2)
            self.assertEqual(self.Settings[self.Opener.SettingsKey][".cpp"]["application"],"old")
            self.Save.assert_not_called()

    def test_reset_selected_and_all_are_persisted(self):
        self.Settings[self.Opener.SettingsKey]={".cpp":{"application":"old"},".hpp":{"application":"other"}}
        self.Opener.ResetAssociations("CPP")
        self.assertNotIn(".cpp",self.Opener.Associations());self.assertIn(".hpp",self.Opener.Associations())
        self.Opener.ResetAssociations();self.assertEqual(self.Opener.Associations(),{})
        self.assertEqual(self.Save.call_count,2)

    def test_open_with_replaces_choice_but_cancel_preserves_it(self):
        with patch("Editor.asset_opening.ChooseApplication", side_effect=["first", None, "second"]), patch("Editor.asset_opening.LaunchApplication") as launch:
            self.Opener.Open(self.Files, None, self.Locale)
            self.Opener.Open(self.Files, None, self.Locale, choose=True)
            self.assertEqual(self.Settings[self.Opener.SettingsKey][".cpp"]["application"], "first")
            self.Opener.Open(self.Files, None, self.Locale, choose=True)
            self.assertEqual(self.Settings[self.Opener.SettingsKey][".cpp"]["application"], "second")
            self.assertEqual(launch.call_count, 2)

    def test_failed_launch_is_not_saved_and_removed_application_reprompts(self):
        with patch("Editor.asset_opening.ChooseApplication", return_value="chosen"), patch("Editor.asset_opening.LaunchApplication", side_effect=OSError("failed")):
            with self.assertRaises(OSError):self.Opener.Open(self.Files, None, self.Locale)
        self.assertNotIn(self.Opener.SettingsKey, self.Settings)
        with patch("Editor.asset_opening.ChooseApplication", return_value="first"), patch("Editor.asset_opening.LaunchApplication"):
            self.Opener.Open(self.Files, None, self.Locale)
        with patch("Editor.asset_opening.ValidateApplication", side_effect=FileNotFoundError()), patch("Editor.asset_opening.ChooseApplication", return_value="second") as choose, patch("Editor.asset_opening.LaunchApplication"):
            self.Opener.Open(self.Files, None, self.Locale)
            choose.assert_called_once()

    def test_registry_is_extendable_and_unsupported_files_never_launch(self):
        self.assertTrue(self.Opener.Supports(self.Files[1]))
        self.assertFalse(self.Opener.Supports("test.bscene"))
        self.Opener.RegisterExtension("hpp")
        self.assertTrue(self.Opener.Supports("test.HPP"))
        with patch("Editor.asset_opening.LaunchApplication") as launch:
            with self.assertRaises(ValueError):self.Opener.Open([self.Root / "test.exe"], None, self.Locale)
            launch.assert_not_called()

    def test_save_failure_rolls_back_association(self):
        self.Save.side_effect = OSError("settings unavailable")
        with patch("Editor.asset_opening.ChooseApplication", return_value="chosen"), patch("Editor.asset_opening.LaunchApplication"):
            with self.assertRaises(OSError):self.Opener.Open(self.Files, None, self.Locale)
        self.assertNotIn(self.Opener.SettingsKey, self.Settings)

    def test_launch_passes_unicode_and_spaces_without_shell(self):
        application = self.Root / "My Editor.exe"
        application.write_bytes(b"test")
        file = self.Root / "Код & player.cpp"
        file.write_text("test", encoding="utf-8")
        with patch("Editor.platform_services.sys.platform", "win32"), patch("Editor.platform_services.subprocess.Popen") as launch:
            LaunchApplication(application, [file])
        self.assertEqual(launch.call_args.args[0], [str(application), str(file.resolve())])
        self.assertFalse(launch.call_args.kwargs["shell"])

    def test_native_picker_cancel_never_validates_or_launches(self):
        dialog = Mock()
        dialog.exec.return_value = QFileDialog.DialogCode.Rejected
        with patch("Editor.platform_services.QFileDialog", return_value=dialog) as factory, patch("Editor.platform_services.ValidateApplication") as validate:
            # Preserve the Qt enum attributes on the mocked constructor.
            for name in ("Option", "AcceptMode", "FileMode", "DialogCode"):
                setattr(factory, name, getattr(QFileDialog, name))
            self.assertIsNone(ChooseApplication(None, self.Locale, ".cpp"))
            validate.assert_not_called()
        dialog.setOption.assert_any_call(QFileDialog.Option.DontUseNativeDialog, False)

    def test_double_click_and_context_actions_route_to_opener(self):
        panel = AssetBrowserPanel(self.Locale)
        opener = Mock();opener.Supports.side_effect = lambda path: Path(path).suffix.lower() == ".cpp"
        panel.ExternalOpener = opener;panel.SetProjectRoot(self.Root)
        try:
            item = next(panel.Browser.item(index) for index in range(panel.Browser.count()) if Path(panel.Browser.item(index).data(Qt.ItemDataRole.UserRole)) == self.Files[0])
            panel._Activate(item)
            opener.Open.assert_called_once_with([self.Files[0]], panel.window(), self.Locale, False)
            panel.Browser.setCurrentItem(item)
            seen = []
            def menu_ready(menu, _position):
                QTimer.singleShot(0, menu, menu.close)
                actions = {action.text(): action for action in menu.actions()}
                for key in ("assets.open", "assets.open_with"):
                    action = actions[self.Locale.Translate(key)]
                    self.assertTrue(action.isVisible());action.trigger();seen.append(key)
            panel.ContextMenuRequested.connect(menu_ready)
            panel._ShowContextMenu(panel.Browser, QPoint(9999, 9999))
            self.assertEqual(seen, ["assets.open", "assets.open_with"])
            self.assertTrue(opener.Open.call_args.args[-1])
            outside = self.Root.parent / "outside.cpp"
            errors = [];panel.AssetOperationFailed.connect(errors.append)
            panel._OpenExternal([outside]);self.assertTrue(errors)
        finally:
            panel.close();panel.deleteLater();self.App.sendPostedEvents(None, QEvent.Type.DeferredDelete)


if __name__ == "__main__":unittest.main()
