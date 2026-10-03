"""Qt-to-runtime input routing. SDL and gameplay state remain native/private."""
import sys
from PySide6.QtCore import QEvent,QObject,Qt
from PySide6.QtWidgets import QApplication
from shiboken6 import isValid

def KeyScancode(event)->int:
    # Windows native make codes preserve physical WASD on non-Latin layouts.
    if sys.platform=="win32" and event.nativeScanCode():
        scan=event.nativeScanCode()
        native={0x1e:4,0x30:5,0x2e:6,0x20:7,0x12:8,0x21:9,0x22:10,0x23:11,0x17:12,0x24:13,0x25:14,0x26:15,0x32:16,0x31:17,0x18:18,0x19:19,0x10:20,0x13:21,0x1f:22,0x14:23,0x16:24,0x2f:25,0x11:26,0x2d:27,0x15:28,0x2c:29,0x2a:225,0x36:229,0x1d:224,0x11d:228,0xe01d:228,0x38:226,0x138:230,0xe038:230}
        if scan in native:return native[scan]
    key=event.key()
    if event.modifiers()&Qt.KeyboardModifier.KeypadModifier:
        if Qt.Key.Key_1<=key<=Qt.Key.Key_9:return 89+key-int(Qt.Key.Key_1)
        if key==Qt.Key.Key_0:return 98
        if key==Qt.Key.Key_Period:return 99
    if Qt.Key.Key_A<=key<=Qt.Key.Key_Z:return 4+key-int(Qt.Key.Key_A)
    if Qt.Key.Key_1<=key<=Qt.Key.Key_9:return 30+key-int(Qt.Key.Key_1)
    if key==Qt.Key.Key_0:return 39
    if Qt.Key.Key_F1<=key<=Qt.Key.Key_F12:return 58+key-int(Qt.Key.Key_F1)
    if Qt.Key.Key_F13<=key<=Qt.Key.Key_F24:return 104+key-int(Qt.Key.Key_F13)
    named={Qt.Key.Key_Return:40,Qt.Key.Key_Enter:88,Qt.Key.Key_Escape:41,Qt.Key.Key_Backspace:42,Qt.Key.Key_Tab:43,Qt.Key.Key_Space:44,Qt.Key.Key_Minus:45,Qt.Key.Key_Equal:46,Qt.Key.Key_BracketLeft:47,Qt.Key.Key_BracketRight:48,Qt.Key.Key_Backslash:49,Qt.Key.Key_Semicolon:51,Qt.Key.Key_Apostrophe:52,Qt.Key.Key_QuoteLeft:53,Qt.Key.Key_Comma:54,Qt.Key.Key_Period:55,Qt.Key.Key_Slash:56,Qt.Key.Key_CapsLock:57,Qt.Key.Key_Insert:73,Qt.Key.Key_Home:74,Qt.Key.Key_PageUp:75,Qt.Key.Key_Delete:76,Qt.Key.Key_End:77,Qt.Key.Key_PageDown:78,Qt.Key.Key_Right:79,Qt.Key.Key_Left:80,Qt.Key.Key_Down:81,Qt.Key.Key_Up:82,Qt.Key.Key_Shift:225,Qt.Key.Key_Control:224,Qt.Key.Key_Alt:226,Qt.Key.Key_Meta:227}
    return named.get(key,0)

class GameInputRouter(QObject):
    def __init__(self,window,runtime):
        super().__init__(window);self.Window=window;self.Runtime=runtime;self.Surface=window.Output.Surface;self.Placeholder=window.Output._no_camera;self._last=None;self._closed=False
        self.Surface.installEventFilter(self)
        window.Output._no_camera.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        window.Output._no_camera.setMouseTracking(True);window.Output._no_camera.installEventFilter(self)
        app=QApplication.instance();app.focusChanged.connect(self._FocusChanged);app.applicationStateChanged.connect(self._StateChanged)
        window.destroyed.connect(self._Disconnect)
    def _Disconnect(self,*_):
        if self._closed:return
        self._closed=True;self.Runtime.SetGameInputActive(False)
        for widget in (self.Surface,self.Placeholder):
            if isValid(widget):widget.removeEventFilter(self)
        app=QApplication.instance()
        if app:
            for signal,slot in ((app.focusChanged,self._FocusChanged),(app.applicationStateChanged,self._StateChanged)):
                try:signal.disconnect(slot)
                except (RuntimeError,TypeError):pass
    def Close(self):self._Disconnect()
    def _FocusChanged(self,*_):self.SyncFocus()
    def _StateChanged(self,*_):self.SyncFocus()
    def FocusGame(self):
        target=self.Window.Output._output_stack.currentWidget();target.setFocus(Qt.FocusReason.OtherFocusReason);self.SyncFocus()
    def SyncFocus(self):
        if self._closed or not self.Runtime.IsPlaying() or self.Runtime.IsPaused():
            self.Runtime.SetGameInputActive(False);self._last=None;return False
        app=QApplication.instance()
        target=self.Window.Output._output_stack.currentWidget()
        active=bool(self.Runtime.IsPlaying() and not self.Runtime.IsPaused() and target.isVisible() and target.hasFocus() and app.activeWindow() is target.window() and app.activeModalWidget() is None)
        self.Runtime.SetGameInputActive(active)
        if not active:self._last=None
        return active
    def eventFilter(self,watched,event):
        kind=event.type()
        if self._closed or kind not in (QEvent.Type.FocusIn,QEvent.Type.FocusOut,QEvent.Type.Hide,QEvent.Type.ShortcutOverride,QEvent.Type.KeyPress,QEvent.Type.KeyRelease,QEvent.Type.MouseButtonPress,QEvent.Type.MouseButtonRelease,QEvent.Type.MouseMove,QEvent.Type.Wheel):return False
        if kind in (QEvent.Type.FocusOut,QEvent.Type.Hide):self.Runtime.SetGameInputActive(False);self._last=None;return False
        if kind==QEvent.Type.FocusIn:self.SyncFocus();return False
        if kind==QEvent.Type.MouseButtonPress:watched.setFocus(Qt.FocusReason.MouseFocusReason)
        if not self.SyncFocus():return False
        if kind==QEvent.Type.ShortcutOverride:
            event.accept();return True  # Gameplay keys must not activate editor shortcuts.
        if kind in (QEvent.Type.KeyPress,QEvent.Type.KeyRelease):
            if event.isAutoRepeat():event.accept();return True
            if event.key()==Qt.Key.Key_Space and event.modifiers()&Qt.KeyboardModifier.ShiftModifier:
                if kind==QEvent.Type.KeyPress:
                    self.Runtime.SetGameInputActive(False);self.Window.ToggleGameMaximized();self.FocusGame()
                event.accept();return True
            self.Runtime.GameKey(KeyScancode(event),kind==QEvent.Type.KeyPress);event.accept();return True
        if kind in (QEvent.Type.MouseButtonPress,QEvent.Type.MouseButtonRelease):
            buttons={Qt.MouseButton.LeftButton:1,Qt.MouseButton.MiddleButton:2,Qt.MouseButton.RightButton:3,Qt.MouseButton.BackButton:4,Qt.MouseButton.ForwardButton:5}
            position=event.position();self.Runtime.GameMotion(position.x(),position.y(),0,0);self._last=position
            self.Runtime.GameButton(buttons.get(event.button(),0),kind==QEvent.Type.MouseButtonPress);event.accept();return True
        if kind==QEvent.Type.MouseMove:
            position=event.position();delta=position-self._last if self._last is not None else position-position;self._last=position
            self.Runtime.GameMotion(position.x(),position.y(),delta.x(),delta.y());event.accept();return True
        if kind==QEvent.Type.Wheel:
            delta=event.angleDelta();self.Runtime.GameScroll(delta.x()/120,delta.y()/120);event.accept();return True
        return False
