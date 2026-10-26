import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
from shiboken6 import isValid

from PySide6.QtCore import QObject,QTimer,Signal,Qt,QPointF,QEvent
from PySide6.QtGui import QMouseEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QMainWindow,QPushButton,QVBoxLayout,QWidget
from Editor.main import EditorSession
from bazzalt.settings import Version


class Controller(QObject):
    FrameCompleted=Signal()
    FrameFailed=Signal(str)
    def __init__(self,parent):
        super().__init__(parent);self.Timer=QTimer(self);self.Timer.start(10000)
    def _ProjectLoaded(self,path):pass
    def RefreshHierarchy(self):pass


class Surface(QPushButton):
    AttachmentFailed=Signal(str)


class Window(QMainWindow):
    def __init__(self,*args):
        super().__init__();self.Controller=Controller(self)
        content=QWidget(self);layout=QVBoxLayout(content);self.setCentralWidget(content)
        self.Button=Surface('Scene',content);self.Button._attached=False;layout.addWidget(self.Button)
        self.Scene=SimpleNamespace(Surface=self.Button)
        self.Output=SimpleNamespace(Surface=Surface('Game',self))
        self.Output.Surface.hide();self.Clicks=0
        self.Button.clicked.connect(self._Clicked)
    def _Clicked(self):self.Clicks+=1


class EditorLoadingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.App.setQuitOnLastWindowClosed(False)
        self.Runtime=Mock();self.Runtime.IsAvailable.return_value=True
        self.Runtime.SupportedRenderingBackends.return_value=['automatic']
        with patch('Editor.main.RuntimeService',return_value=self.Runtime):
            self.Session=EditorSession(self.App,Path('Test.bproject'),Version.Parse('0.7.0'),{})
        self.Session.Loading.show();self.App.processEvents()
    def tearDown(self):
        session=self.Session;session._ReleaseStartupInput()
        if session.EditorWindow is not None:
            session.EditorWindow.Controller.Timer.stop();session.EditorWindow.hide();session.EditorWindow.deleteLater()
        if isValid(session.Loading):session.Loading.hide();session.Loading.deleteLater()
        session.deleteLater();self.App.processEvents()
        self.App.setQuitOnLastWindowClosed(False)
    def _Open(self):
        with patch('Editor.main.Editor',Window):self.Session.OpenEditor({'name':'Test'})
        self.App.processEvents();return self.Session.EditorWindow

    def test_loading_is_modal_and_cannot_be_dismissed_early(self):
        loading=self.Session.Loading
        self.assertEqual(loading.windowModality(),Qt.WindowModality.ApplicationModal)
        QTest.keyClick(loading,Qt.Key.Key_Escape);loading.close();self.App.processEvents()
        self.assertTrue(loading.isVisible())

    def test_loading_stays_centered_on_editor_after_maximize_resize_and_move(self):
        window=self._Open();loading=self.Session.Loading
        def check():
            self.App.processEvents();QTest.qWait(1)
            delta=loading.frameGeometry().center()-window.frameGeometry().center()
            self.assertLessEqual(abs(delta.x()),1);self.assertLessEqual(abs(delta.y()),1)
        check()
        window.showNormal();window.resize(720,540);window.move(-120,70);check()
        window.resize(920,650);window.move(80,120);check()
        window.showMaximized();check()

    def test_waits_for_attached_viewport_and_discards_queued_input(self):
        window=self._Open();session=self.Session
        with patch('Editor.main.QGuiApplication.platformName',return_value='windows'):
            window.Controller.FrameCompleted.emit();window.Controller.FrameCompleted.emit()
            self.assertEqual(session._readyFrames,0)
            self.assertFalse(window.isEnabled());self.assertTrue(session.Loading.isVisible())
            window.Button._attached=True;window.Controller.FrameCompleted.emit()
            self.assertFalse(window.isEnabled())
            window.Controller.FrameCompleted.emit()
        # Simulate clicks accumulated while the GUI thread was initializing.
        for kind,button,buttons in (
            (QEvent.Type.MouseButtonPress,Qt.MouseButton.LeftButton,Qt.MouseButton.LeftButton),
            (QEvent.Type.MouseButtonRelease,Qt.MouseButton.LeftButton,Qt.MouseButton.NoButton)):
            self.App.postEvent(window.Button,QMouseEvent(kind,QPointF(4,4),QPointF(4,4),button,buttons,Qt.KeyboardModifier.NoModifier))
        self.App.processEvents();self.App.processEvents()
        self.assertEqual(window.Clicks,0);self.assertTrue(window.isEnabled())
        self.assertIsNone(session._startupInput);self.assertTrue(window.Controller.Timer.isActive())
        QTest.mouseClick(window.Button,Qt.MouseButton.LeftButton)
        self.assertEqual(window.Clicks,1)

    def test_frame_failure_keeps_editor_blocked_and_cancels_pending_completion(self):
        window=self._Open();session=self.Session
        with TemporaryDirectory() as folder,patch('Editor.main.DataPaths.Logs',return_value=Path(folder)),patch('Editor.main.QMessageBox.critical') as message,patch.object(self.App,'quit') as quit:
            session._finishing=True
            window.Controller.FrameFailed.emit('Renderer initialization failed')
            session._FinishStartup()
            self.assertTrue(session._startupFailed);self.assertFalse(window.isEnabled())
            self.assertFalse(window.Controller.Timer.isActive());self.assertIsNone(session._startupInput)
            self.assertIn('Renderer initialization failed',(Path(folder)/'editor-startup.log').read_text(encoding='utf-8'))
            message.assert_called_once();quit.assert_called_once()

    def test_completion_flushes_posted_input_when_native_event_pump_defers_it(self):
        window=self._Open();session=self.Session;session._finishing=True
        callbacks=[];QTimer.singleShot(0,lambda:callbacks.append('preserved'))
        for kind,buttons in ((QEvent.Type.MouseButtonPress,Qt.MouseButton.LeftButton),(QEvent.Type.MouseButtonRelease,Qt.MouseButton.NoButton)):
            self.App.postEvent(window.Button,QMouseEvent(kind,QPointF(4,4),QPointF(4,4),Qt.MouseButton.LeftButton,buttons,Qt.KeyboardModifier.NoModifier))
        # A platform dispatcher can defer posted events during nested pumping.
        # Force that ordering so the regression also reproduces on Windows.
        with patch.object(self.App,'processEvents'):session._FinishStartup()
        self.App.processEvents()
        self.assertTrue(window.isEnabled());self.assertEqual(window.Clicks,0)
        self.assertEqual(callbacks,['preserved'])
        QTest.mouseClick(window.Button,Qt.MouseButton.LeftButton)
        self.assertEqual(window.Clicks,1)
