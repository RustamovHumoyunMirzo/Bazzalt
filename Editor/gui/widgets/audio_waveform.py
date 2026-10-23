"""Bounded native PCM analysis off the UI thread, and a themed timeline."""
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from PySide6.QtCore import Qt,QPointF,Signal
from PySide6.QtGui import QPainter,QPalette,QPen
from PySide6.QtWidgets import QWidget,QSizePolicy

_Workers=ThreadPoolExecutor(max_workers=2,thread_name_prefix='audio-waveform')
_Cache=OrderedDict()

def RequestWaveform(builder,path):
    if not callable(builder) or not path:return None
    try:
        source=Path(path);stat=source.stat();key=(str(source.resolve()),stat.st_size,stat.st_mtime_ns)
    except OSError:return None
    if key in _Cache:_Cache.move_to_end(key);return _Cache[key]
    # Avoid an unbounded queue when rapidly selecting clips.
    if sum(not future.done() for future in _Cache.values())>=4:return None
    future=_Workers.submit(builder,str(source),512);_Cache[key]=future
    while len(_Cache)>32:
        old,previous=next(iter(_Cache.items()))
        if not previous.done():break
        _Cache.pop(old)
    return future

class AudioWaveform(QWidget):
    SeekRequested=Signal(float)
    def __init__(self,parent=None):
        super().__init__(parent);self.Peaks=[];self.Fraction=0.;self.Dragging=False;self.setMinimumHeight(72);self.setSizePolicy(QSizePolicy.Policy.Expanding,QSizePolicy.Policy.Fixed);self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
    def SetPeaks(self,peaks):self.Peaks=[(max(-1.,float(low)),min(1.,float(high))) for low,high in peaks];self.update()
    def SetFraction(self,value):self.Fraction=max(0.,min(1.,value));self.update()
    def paintEvent(self,event):
        painter=QPainter(self);palette=self.palette();painter.fillRect(self.rect(),palette.color(QPalette.ColorRole.Base));mid=self.height()/2;span=mid-7;left=7;width=max(1,self.width()-14)
        painter.setPen(QPen(palette.color(QPalette.ColorRole.Mid),1));painter.drawLine(QPointF(left,mid),QPointF(left+width,mid))
        if self.Peaks:
            for pixel in range(int(width)):
                begin=int(pixel*len(self.Peaks)/width);end=max(begin+1,int((pixel+1)*len(self.Peaks)/width));segment=self.Peaks[begin:end]
                if not segment:continue
                low=min(v[0] for v in segment);high=max(v[1] for v in segment)
                painter.setPen(palette.color(QPalette.ColorRole.Highlight if pixel/width<=self.Fraction else QPalette.ColorRole.Text));painter.drawLine(QPointF(left+pixel,mid-high*span),QPointF(left+pixel,mid-low*span))
        painter.setPen(QPen(palette.color(QPalette.ColorRole.Highlight),1));x=left+width*self.Fraction;painter.drawLine(QPointF(x,4),QPointF(x,self.height()-4))
    def _Seek(self,event):self.SeekRequested.emit(max(0.,min(1.,(event.position().x()-7)/max(1,self.width()-14))));event.accept()
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self.Dragging=True;self.setFocus();self._Seek(event)
        else:super().mousePressEvent(event)
    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.LeftButton:self._Seek(event)
        else:super().mouseMoveEvent(event)
    def mouseReleaseEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self.Dragging=False;self._Seek(event)
        else:super().mouseReleaseEvent(event)
