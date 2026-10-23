import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from PySide6.QtCore import Qt,QMimeData,QUrl,QPointF,QEvent
from PySide6.QtGui import QDropEvent,QMouseEvent
from PySide6.QtWidgets import QApplication,QAbstractItemView
from Editor.gui.panels.assets import AssetBrowserPanel
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager

class AssetDragImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def Panel(self,folder):
        resources=ResourceManager();panel=AssetBrowserPanel(LocalizationManager(resources),resources);panel.SetProjectRoot(folder);panel.resize(600,350);panel.show();self.App.processEvents();self.addCleanup(panel.deleteLater);return panel
    def test_external_urls_copy_files_and_directories_without_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);assets=root/'Assets';assets.mkdir();outside=root/'Outside';outside.mkdir();(outside/'音声.MP3').write_bytes(b'external');(assets/'音声.MP3').write_bytes(b'existing');(outside/'Folder').mkdir();(outside/'Folder'/'Clip.wav').write_bytes(b'clip');(outside/'Folder'/'Clip.wav.meta').write_text('foreign UUID')
            panel=self.Panel(assets);mime=QMimeData();mime.setUrls([QUrl.fromLocalFile(str(outside/'音声.MP3')),QUrl.fromLocalFile(str(outside/'Folder'))]);event=QDropEvent(QPointF(500,260),Qt.DropAction.CopyAction,mime,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier);panel.Browser.dropEvent(event)
            self.assertTrue(event.isAccepted());self.assertEqual((assets/'音声.MP3').read_bytes(),b'existing');self.assertEqual((assets/'音声 1.MP3').read_bytes(),b'external');self.assertTrue((assets/'Folder'/'Clip.wav').exists());self.assertFalse((assets/'Folder'/'Clip.wav.meta').exists());self.assertTrue((outside/'音声.MP3').exists())
    def test_import_rejects_outside_destination_and_recursive_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);assets=root/'Assets';assets.mkdir();source=root/'file.mp3';source.write_bytes(b'clip');panel=self.Panel(assets);errors=[];panel.AssetOperationFailed.connect(errors.append)
            self.assertEqual(panel.ImportPaths([source],root),[]);self.assertEqual(panel.ImportPaths([root]),[]);self.assertTrue(errors);self.assertFalse((assets/root.name).exists())
    def test_picker_can_resolve_new_files_without_inspector_reselection(self):
        from Editor.gui.widgets.fields import AssetPickerInput
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'New.mp3';picker=AssetPickerInput(accepted_extensions={'.mp3'});self.addCleanup(picker.deleteLater)
            record=dict(uuid='12345678-1234-4234-8234-123456789abc',name=path.name,path=str(path),extension='.mp3')
            picker.AssetResolver=lambda value:record if str(value)==str(path) else None
            mime=QMimeData();mime.setData('application/x-bazzalt-asset',str(path).encode());event=QDropEvent(QPointF(2,2),Qt.DropAction.CopyAction,mime,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier);picker.dropEvent(event);self.assertTrue(event.isAccepted());self.assertEqual(picker.GetValue(),record['uuid'])
    def test_drag_does_not_dispatch_inspector_selection_but_click_does(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'clip.mp3').write_bytes(b'clip');panel=self.Panel(root);selected=[];cleared=[];panel.AssetSelected.connect(selected.append);panel.SelectionCleared.connect(lambda:cleared.append(True));item=panel.Browser.item(0);point=QPointF(panel.Browser.visualItemRect(item).center())
            def mouse(kind,buttons):return QMouseEvent(kind,point,point,Qt.MouseButton.LeftButton,buttons,Qt.KeyboardModifier.NoModifier)
            panel.Browser.mousePressEvent(mouse(QEvent.Type.MouseButtonPress,Qt.MouseButton.LeftButton));self.assertEqual(selected,[]);self.assertEqual(cleared,[])
            with patch.object(QAbstractItemView,'startDrag',return_value=None):panel.Browser.startDrag(Qt.DropAction.CopyAction)
            panel.Browser.mouseReleaseEvent(mouse(QEvent.Type.MouseButtonRelease,Qt.MouseButton.NoButton));self.assertEqual(selected,[]);self.assertEqual(cleared,[])
            panel.Browser.mousePressEvent(mouse(QEvent.Type.MouseButtonPress,Qt.MouseButton.LeftButton));panel.Browser.mouseReleaseEvent(mouse(QEvent.Type.MouseButtonRelease,Qt.MouseButton.NoButton));self.assertEqual(selected,[root/'clip.mp3'])
            mime=panel.Browser.mimeData([item]);self.assertTrue(mime.hasUrls());self.assertTrue(mime.hasFormat('application/x-bazzalt-asset'))
    def test_audio_icons_show_their_inner_artwork(self):
        resources=ResourceManager()
        for name in ('audio','audiomixer'):
            image=resources.Icon('icons/abrowser/'+name+'.svg').pixmap(141,172).toImage();colors={image.pixelColor(x,y).rgba() for x in range(30,110,4) for y in range(45,125,4)};self.assertGreater(len(colors),4,name)
        with tempfile.TemporaryDirectory() as folder:
            panel=self.Panel(folder)
            for suffix,icon in (('.mp3','audio'),('.MP3','audio'),('.bmixer','audiomixer')):self.assertEqual(panel._Icon(Path(folder)/('Asset'+suffix)).cacheKey(),panel._resources.Icon('icons/abrowser/'+icon+'.svg').cacheKey())
    def test_import_issues_override_icons_without_changing_selection_and_recover(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Broken.mp3';path.write_bytes(b'invalid');panel=self.Panel(folder);item=panel.Browser.item(0);item.setSelected(True)
            changes=[];panel.AssetSelected.connect(changes.append)
            panel.AssetInfoProvider=lambda _:dict(error='Unsupported audio container')
            panel.UpdateAssetIssues()
            self.assertEqual(item.icon().cacheKey(),panel._resources.Icon('icons/abrowser/warn.svg').cacheKey())
            self.assertIn('Unsupported audio container',item.toolTip());self.assertTrue(item.isSelected());self.assertEqual(changes,[])
            panel.AssetInfoProvider=lambda _:dict(error='');panel.UpdateAssetIssues()
            self.assertEqual(item.icon().cacheKey(),panel._resources.Icon('icons/abrowser/audio.svg').cacheKey());self.assertNotIn('Unsupported',item.toolTip());self.assertTrue(item.isSelected())
    def test_failed_asset_inspector_shows_issue_without_starting_preview(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        from Editor.gui.controller import EditorController
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Broken.mp3';path.write_bytes(b'invalid')
            issue='Unsupported MP4/M4A container';runtime=Mock();runtime.AssetInfo.return_value={'error':issue}
            section=Mock();properties=Mock();properties.AddComponentSection.return_value=section
            resources=ResourceManager();window=SimpleNamespace(Scene=Mock(),Properties=properties,Localization=LocalizationManager(resources),AssetBrowser=Mock())
            controller=SimpleNamespace(Runtime=runtime,Window=window,_UpdateGizmo=Mock(),_FormatAssetSize=str,_AssetSize=lambda _:7,_InspectAudioClip=Mock())
            EditorController.SelectAsset(controller,path)
            controller._InspectAudioClip.assert_not_called();self.assertEqual(section.AddWideWidget.call_args.args[0].text(),issue)
            self.assertIsNone(controller._audio_preview)
            self.assertEqual(properties.AddComponentSection.call_args.args[0],'asset.issue')
    def test_activating_failed_audio_does_not_reuse_a_deleted_preview(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        from Editor.gui.controller import EditorController
        previous=Mock()
        controller=SimpleNamespace(_audio_preview=previous)
        controller.SelectAsset=lambda path:setattr(controller,'_audio_preview',None)
        EditorController.ActivateAsset(controller,Path('Invalid.mp3'))
        previous.Play.assert_not_called()
