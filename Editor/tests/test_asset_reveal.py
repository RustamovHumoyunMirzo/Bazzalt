import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import ctypes
from pathlib import Path
from unittest.mock import Mock,patch
import unittest
from PySide6.QtCore import QSize,QPoint,Qt,QTimer
from PySide6.QtWidgets import QApplication
from Editor.platform_services import RevealFiles,_RevealWindows
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.gui.panels.assets import AssetBrowserPanel


class AssetRevealTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])

    def test_cpp_icon_uses_same_tall_proportions_as_other_file_icons(self):
        resources=ResourceManager()
        cpp=resources.Icon("icons/abrowser/nativecpp.svg").pixmap(QSize(48,48))
        shader=resources.Icon("icons/abrowser/shader.svg").pixmap(QSize(48,48))
        self.assertLess(cpp.width(),cpp.height())
        self.assertEqual(cpp.size(),shader.size())

    def test_windows_selects_multiple_files_in_one_folder_and_frees_pidls(self):
        shell=Mock();ole=Mock();ole.CoInitializeEx.return_value=0
        shell.SHOpenFolderAndSelectItems.return_value=0
        shell.ILFindLastID.side_effect=lambda pidl:pidl.value+1
        def parse(_path,_bind,output,_flags,_attributes):
            output._obj.value=100+shell.SHParseDisplayName.call_count*10
            return 0
        shell.SHParseDisplayName.side_effect=parse
        root=Path(__file__).parent/"fixtures/assets"
        with patch.object(ctypes,"WinDLL",side_effect=[shell,ole],create=True):
            _RevealWindows([root/"player.png",root/"player.png.meta"])
        self.assertEqual(shell.SHOpenFolderAndSelectItems.call_count,1)
        self.assertEqual(shell.SHOpenFolderAndSelectItems.call_args.args[1],2)
        self.assertEqual(ole.CoTaskMemFree.call_count,3)
        ole.CoUninitialize.assert_called_once()

    def test_windows_releases_resources_even_if_explorer_fails(self):
        shell=Mock();ole=Mock();ole.CoInitializeEx.return_value=0
        shell.SHOpenFolderAndSelectItems.return_value=-1
        shell.ILFindLastID.side_effect=lambda pidl:pidl.value+1
        def parse(_path,_bind,output,_flags,_attributes):output._obj.value=100;return 0
        shell.SHParseDisplayName.side_effect=parse
        with patch.object(ctypes,"WinDLL",side_effect=[shell,ole],create=True):
            with self.assertRaises(OSError):_RevealWindows([Path("asset.cpp")])
        self.assertEqual(ole.CoTaskMemFree.call_count,2)
        ole.CoUninitialize.assert_called_once()

    def test_reveal_validates_paths_and_deduplicates_without_launching_explorer(self):
        path=Path(__file__).parent/"fixtures/assets/player.png"
        with patch("Editor.platform_services.sys.platform","win32"),patch("Editor.platform_services._RevealWindows") as reveal:
            RevealFiles([path,path]);reveal.assert_called_once_with([path.resolve()])
            with self.assertRaises(FileNotFoundError):RevealFiles([path.with_name("does-not-exist")])

    def test_context_command_keeps_multiple_selected_assets(self):
        resources=ResourceManager();locale=LocalizationManager(resources)
        panel=AssetBrowserPanel(locale,resources);panel.SetProjectRoot(Path(__file__).parent/"fixtures/assets")
        for index in range(panel.Browser.count()):panel.Browser.item(index).setSelected(True)
        expected={Path(item.data(Qt.ItemDataRole.UserRole)) for item in panel.Browser.selectedItems()}
        def trigger(menu,_position):
            key="assets.reveal" if os.name=="nt" else "assets.reveal_finder" if __import__("sys").platform=="darwin" else "assets.reveal_manager"
            action=next(action for action in menu.actions() if action.text()==locale.Translate(key))
            self.assertTrue(action.isEnabled());action.trigger()
            QTimer.singleShot(0,menu.close)
        panel.ContextMenuRequested.connect(trigger)
        with patch.object(panel.Browser,"itemAt",return_value=panel.Browser.item(0)),patch("Editor.gui.panels.assets.RevealFiles") as reveal:
            panel._ShowContextMenu(panel.Browser,QPoint())
            self.assertEqual(set(reveal.call_args.args[0]),expected)
        panel.close()
