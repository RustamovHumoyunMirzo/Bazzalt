"""Read-only mixer inspector: audition a project clip, never edit routing here."""
import math
from PySide6.QtCore import QTimer,Qt,QRectF
from PySide6.QtGui import QPainter,QPalette
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QLabel
from .audio import AudioPreview
from .fields import AssetPickerInput,EnumInput,RangeInput
from ..panels.mixer_controls import MeterWidget,DbToPosition,TRACK_MARGIN
from ...theme import Theme

class LevelScale(QWidget):
    """Read-only dBFS markings aligned with the stereo meter."""
    def __init__(self,parent=None):super().__init__(parent);self.setFixedWidth(38);self.setMinimumHeight(130)
    def paintEvent(self,event):
        painter=QPainter(self);painter.setPen(self.palette().color(QPalette.ColorRole.Text))
        for db in (20,10,0,-10,-20,-40,-60,-80):
            y=TRACK_MARGIN+(self.height()-2*TRACK_MARGIN)*(1-DbToPosition(db))
            painter.drawText(QRectF(0,y-7,36,14),Qt.AlignmentFlag.AlignRight|Qt.AlignmentFlag.AlignVCenter,str(db))

class MixerAssetPreview(QWidget):
    def __init__(self,runtime,identity,buses,locale,assets,theme_manager=None,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Identity=identity;self.Player=None;self.Locale=locale
        root=QVBoxLayout(self);root.setContentsMargins(0,0,0,0);tr=locale.Translate
        self.Clip=AssetPickerInput(tr('audio.clip'),accepted_extensions={'.wav','.mp3','.flac','.ogg','.oga'},picker_title=tr('audio.clip'),search_placeholder=tr('properties.search_project_assets'),missing_label=tr('properties.missing_asset'))
        self.Clip.ConfigureAssets(assets);self.Clip.PickRequested.connect(self.Clip.OpenProjectPicker);self.Clip.ValueChanged.connect(self.SetClip);root.addWidget(self.Clip)
        self.Bus=EnumInput();self.Bus.SetOptions(tuple((b['name'],b['id']) for b in buses));root.addWidget(QLabel(tr('audio.preview_bus')));root.addWidget(self.Bus)
        self.Volume=RangeInput(0,1,1,2);self.Volume.ValueChanged.connect(lambda value:runtime.AudioControl(self.Player.Voice,'volume',value) if self.Player else None);root.addWidget(QLabel(tr('audio.preview_volume')));root.addWidget(self.Volume)
        self.PlayerHost=QVBoxLayout();root.addLayout(self.PlayerHost)
        theme=theme_manager.GetTheme() if theme_manager else Theme.dark();self.Meter=MeterWidget(theme);self.Meter.setMinimumHeight(130);row=QHBoxLayout();row.addWidget(LevelScale());row.addWidget(self.Meter);row.addStretch();root.addLayout(row)
        if theme_manager:theme_manager.ThemeChanged.connect(self.Meter.SetTheme)
        self.Timer=QTimer(self);self.Timer.setInterval(50);self.Timer.timeout.connect(self.Refresh);self.Timer.start()
    def SetClip(self,identity):
        if self.Player:self.Player.Stop();self.Player.Timer.stop();self.Player.deleteLater();self.Player=None
        info=self.Runtime.AudioClipInfo(identity)
        if not info:return
        outer=self
        class PreviewRuntime:
            def PreviewAudio(self,clip):
                voice=outer.Runtime.PreviewMixerAudio(outer.Identity,clip,outer.Bus.GetValue());outer.Runtime.AudioControl(voice,'volume',outer.Volume.GetValue());return voice
            def __getattr__(self,key):return getattr(outer.Runtime,key)
        self.Player=AudioPreview(PreviewRuntime(),info['uuid'],self.Locale,info['duration']);self.PlayerHost.addWidget(self.Player)
    def Refresh(self):
        if not self.isVisible():return
        levels=self.Runtime.AudioMixerLevels(self.Identity).get('00000000-0000-0000-0000-000000000000',(0,0));self.Meter.SetLevels(*(max(-80,20*math.log10(max(1e-8,v))) for v in levels));self.Meter.Tick(.05)
