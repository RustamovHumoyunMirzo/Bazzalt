import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication, QWidget
from PySide6.QtCore import QTimer, QEvent, Qt
from PySide6.QtTest import QTest
from Editor.gui.widgets.tags import TagInput

class TagInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_selection_and_catalogue_do_not_scan_entity_snapshots(self):
        runtime=Mock();runtime.ProjectInfo.return_value={'project_path':'project','name':'Test','properties':{}}
        runtime.TagNames.return_value=['Enemy','Flying']
        controller=SimpleNamespace(Runtime=runtime,Window=SimpleNamespace(Localization=SimpleNamespace(Translate=lambda key:key)))
        changed=Mock();field=TagInput(controller,['Enemy'],changed)
        self.assertEqual(field.GetValue(),'Enemy')
        field._Add('Player')
        self.assertEqual(set(field.GetValues()),{'Enemy','Player'})
        runtime.UpdateProjectInfo.assert_called_once()
        runtime.Entities.assert_not_called()
        field.SetValue('Flying');self.assertEqual(field.GetValue(),'Flying')
        field.SetValue(['RuntimeTag']);self.assertEqual(field.GetValue(),'RuntimeTag')
        field.SetValue(['\u00a0Tag']);self.assertEqual(field.GetValue(),'\u00a0Tag')
        self.assertFalse(field._Valid('bad,tag'));self.assertFalse(field._Valid(' bad'))
        field.deleteLater()

    def test_popup_is_lazy_and_checkable(self):
        runtime=Mock();runtime.ProjectInfo.return_value={};runtime.TagNames.return_value=['Enemy','Flying']
        controller=SimpleNamespace(Runtime=runtime,Window=SimpleNamespace(Localization=SimpleNamespace(Translate=lambda key:key)))
        field=TagInput(controller,['Enemy'],Mock())
        self.assertEqual(field.findChildren(QWidget),[])
        field.show();self.assertFalse(field.grab().isNull())
        def toggle():
            action=next(action for action in field._menu.actions() if action.data()=='Flying')
            action.trigger();field._menu.close()
        QTimer.singleShot(0,toggle)
        QTest.mouseClick(field,Qt.MouseButton.LeftButton)
        self.assertEqual(field.GetValues(),('Enemy','Flying'))
        self.assertIsNone(field._menu)
        field.close();field.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
