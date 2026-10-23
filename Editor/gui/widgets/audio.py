"""Audio authoring controls. Playback and decoding remain native, not Qt Media."""
from PySide6.QtCore import QSignalBlocker,QTimer,Signal,Qt,QEvent
from PySide6.QtGui import QPainter,QPalette,QIcon
from PySide6.QtWidgets import QWidget,QHBoxLayout,QVBoxLayout,QPushButton,QLabel,QSlider,QToolButton,QStyle
from .audio_waveform import AudioWaveform,RequestWaveform
from .fields import RangeInput,Vec2Input

class AudioPreview(QWidget):
    def __init__(self,runtime,clip,localization,duration,parent=None,waveform_path=None):
        super().__init__(parent);self.Runtime=runtime;self.Clip=clip;self.Voice=0;self.Attempted=False;self.Duration=max(.001,float(duration));self.Cursor=0.;self.Locale=localization;self._future=None;self._waveform_loaded=False
        tr=localization.Translate;layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0);layout.setSpacing(4)
        self.Waveform=AudioWaveform();self.Waveform.setAccessibleName(tr('audio.waveform'));self.Waveform.SeekRequested.connect(lambda fraction:self.Seek(fraction*self.Duration));layout.addWidget(self.Waveform)
        row=QHBoxLayout();row.setSpacing(3);layout.addLayout(row);self.Buttons=[]
        for key,callback,icon in (('play',self.Play,QStyle.StandardPixmap.SP_MediaPlay),('pause',self.Pause,QStyle.StandardPixmap.SP_MediaPause),('stop',self.Stop,QStyle.StandardPixmap.SP_MediaStop)):
            button=QToolButton();button.setToolTip(tr('audio.'+key));button.setAccessibleName(tr('audio.'+key));button.setFixedSize(24,24);button.clicked.connect(callback);row.addWidget(button);self.Buttons.append((button,icon))
        self.Position=QSlider(Qt.Orientation.Horizontal);self.Position.setRange(0,100000);self.Position.setMinimumWidth(20);self.Position.setAccessibleName(tr('audio.seek'));self.Position.valueChanged.connect(lambda value:self.Seek(value/100000*self.Duration));row.addWidget(self.Position,1)
        self.Time=QLabel();layout.addWidget(self.Time)
        self.Error=QLabel();self.Error.setWordWrap(True);layout.addWidget(self.Error)
        self.WaveformPath=waveform_path
        if not self.WaveformPath and hasattr(runtime,'AudioClipInfo'):
            info=runtime.AudioClipInfo(clip)
            if isinstance(info,dict):self.WaveformPath=info.get('waveform_path')
        self._UpdateIcons();self._UpdatePosition()
        self.Timer=QTimer(self);self.Timer.setInterval(100);self.Timer.timeout.connect(self.Refresh);self.Timer.start();self.Refresh()
    def _UpdateIcons(self):
        for button,standard in self.Buttons:
            pixmap=self.style().standardIcon(standard).pixmap(16,16);painter=QPainter(pixmap);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),self.palette().color(QPalette.ColorRole.ButtonText));painter.end();button.setIcon(QIcon(pixmap))
    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type()==QEvent.Type.PaletteChange and hasattr(self,'Buttons'):self._UpdateIcons()
    def Play(self):
        self.Attempted=True
        state=self.Runtime.AudioStatus(self.Voice).get('state') if self.Voice else 0
        if state==1:self.Refresh();return
        if state==2:self.Runtime.AudioControl(self.Voice,'resume')
        else:
            if self.Cursor>=self.Duration:self.Cursor=0
            self.Voice=self.Runtime.PreviewAudio(self.Clip)
            if self.Voice and self.Cursor>0:self.Runtime.AudioControl(self.Voice,'seek',self.Cursor)
        self.Refresh()
    def Pause(self):self.Runtime.AudioControl(self.Voice,'pause');self.Refresh()
    def Stop(self):self.Runtime.StopAudioPreview();self.Voice=0;self.Attempted=False;self.Cursor=0;self.Refresh()
    def Seek(self,seconds):
        self.Cursor=max(0.,min(self.Duration,seconds))
        if self.Voice:self.Runtime.AudioControl(self.Voice,'seek',self.Cursor)
        self._UpdatePosition()
    @staticmethod
    def _Time(seconds):
        seconds=max(0,int(seconds));minutes,seconds=divmod(seconds,60);return f'{minutes}:{seconds:02d}'
    def _UpdatePosition(self):
        if not self.Position.isSliderDown():
            blocker=QSignalBlocker(self.Position);self.Position.setValue(round(self.Cursor/self.Duration*100000));del blocker
        self.Waveform.SetFraction(self.Cursor/self.Duration);self.Time.setText(f'{self._Time(self.Cursor)} / {self._Time(self.Duration)}')
    def Refresh(self):
        status=self.Runtime.AudioStatus(self.Voice) if self.Attempted else {}
        if status.get('valid') and not self.Position.isSliderDown() and not self.Waveform.Dragging:self.Cursor=min(self.Duration,max(0.,status.get('time',0)))
        elif self.Attempted and status.get('state')==0 and self.Cursor>0:self.Cursor=self.Duration
        self._UpdatePosition()
        if self._future is None and not self._waveform_loaded:self._future=RequestWaveform(getattr(self.Runtime,'WaveformBuilder',None),self.WaveformPath)
        if self._future is not None and self._future.done():
            try:self.Waveform.SetPeaks(self._future.result())
            except Exception:self.Waveform.SetPeaks([])
            self._future=None;self._waveform_loaded=True
        message=status.get('error','') if self.Attempted and not status.get('valid') else ''
        if not message and self._future is not None:message=self.Locale.Translate('audio.waveform_loading')
        self.Error.setText(message);self.Error.setVisible(bool(message))

class AudioCurveInput(QWidget):
    ValueChanged=Signal(object)
    def __init__(self,points,localization,parent=None):
        super().__init__(parent);self.Points=[tuple(p) for p in points];self.Locale=localization
        self.Layout=QVBoxLayout(self);self.Layout.setContentsMargins(0,0,0,0);self.Layout.setSpacing(4);self.Rebuild()
    def GetValue(self):return list(self.Points)
    def SetValue(self,value):
        if self.Points!=[tuple(p) for p in value]:self.Points=[tuple(p) for p in value];self.Rebuild()
    def Rebuild(self):
        while self.Layout.count():
            item=self.Layout.takeAt(0)
            if item.widget():item.widget().deleteLater()
        for index,point in enumerate(self.Points):
            row=QWidget(self);layout=QHBoxLayout(row);layout.setContentsMargins(0,0,0,0)
            field=Vec2Input(point);layout.addWidget(field);field.ValueChanged.connect(lambda v,i=index:self.Edit(i,v))
            if index not in (0,len(self.Points)-1):
                remove=QPushButton("−");remove.setToolTip(self.Locale.Translate("audio.remove_point"));remove.setFixedWidth(24);remove.clicked.connect(lambda _,i=index:self.Remove(i));layout.addWidget(remove)
            self.Layout.addWidget(row)
        add=QPushButton(self.Locale.Translate("audio.add_point"));add.setEnabled(len(self.Points)<64);add.clicked.connect(self.Add);self.Layout.addWidget(add)
    def Edit(self,index,value):
        x,y=map(float,value);previous=-.001 if index==0 else self.Points[index-1][0];following=1.001 if index==len(self.Points)-1 else self.Points[index+1][0]
        if not(0<=x<=1 and 0<=y<=1 and previous<x<following) or (index==0 and x!=0) or (index==len(self.Points)-1 and x!=1):self.Rebuild();return
        self.Points[index]=(x,y);self.ValueChanged.emit(self.GetValue())
    def Add(self):
        if len(self.Points)>=64:return
        index=max(range(len(self.Points)-1),key=lambda i:self.Points[i+1][0]-self.Points[i][0]);a,b=self.Points[index:index+2];self.Points.insert(index+1,((a[0]+b[0])/2,(a[1]+b[1])/2));self.Rebuild();self.ValueChanged.emit(self.GetValue())
    def Remove(self,index):self.Points.pop(index);self.Rebuild();self.ValueChanged.emit(self.GetValue())
