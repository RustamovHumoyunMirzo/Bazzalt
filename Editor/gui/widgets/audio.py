"""Audio authoring controls. Playback and decoding remain native, not Qt Media."""
from PySide6.QtCore import QSignalBlocker,QTimer,Signal
from PySide6.QtWidgets import QWidget,QHBoxLayout,QVBoxLayout,QPushButton,QLabel
from .fields import RangeInput,Vec2Input

class AudioPreview(QWidget):
    def __init__(self,runtime,clip,localization,duration,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Clip=clip;self.Voice=0;self.Attempted=False
        tr=localization.Translate;layout=QVBoxLayout(self);layout.setContentsMargins(0,0,0,0);layout.setSpacing(5)
        row=QHBoxLayout();layout.addLayout(row)
        for key,callback in (("play",self.Play),("pause",self.Pause),("stop",self.Stop)):
            button=QPushButton(tr("audio."+key));button.clicked.connect(callback);row.addWidget(button)
        self.Position=RangeInput(0,max(.001,duration),0,2);self.Position.ValueChanged.connect(lambda v:self.Runtime.AudioControl(self.Voice,"seek",v));layout.addWidget(self.Position)
        self.Error=QLabel();self.Error.setWordWrap(True);layout.addWidget(self.Error)
        self.Timer=QTimer(self);self.Timer.setInterval(100);self.Timer.timeout.connect(self.Refresh);self.Timer.start()
    def Play(self):
        self.Attempted=True
        if self.Voice and self.Runtime.AudioStatus(self.Voice).get("state")==2:self.Runtime.AudioControl(self.Voice,"resume")
        else:self.Voice=self.Runtime.PreviewAudio(self.Clip)
        self.Refresh()
    def Pause(self):self.Runtime.AudioControl(self.Voice,"pause");self.Refresh()
    def Stop(self):self.Runtime.StopAudioPreview();self.Voice=0;self.Attempted=False;self.Refresh()
    def Refresh(self):
        status=self.Runtime.AudioStatus(self.Voice) if self.Attempted else {}
        if not self.Position.Slider.isSliderDown():
            blocker=QSignalBlocker(self.Position);self.Position.SetValue(status.get("time",0));del blocker
        self.Error.setText(status.get("error","") if self.Attempted and not status.get("valid") else "")

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
