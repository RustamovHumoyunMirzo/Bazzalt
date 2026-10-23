"""Read-only mixer inspector: audition a project clip, never edit routing here."""
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QLabel
from .audio import AudioPreview
from .fields import AssetPickerInput,EnumInput,RangeInput

class MixerAssetPreview(QWidget):
    def __init__(self,runtime,identity,buses,locale,assets,theme_manager=None,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Identity=identity;self.Player=None;self.Locale=locale
        root=QVBoxLayout(self);root.setContentsMargins(0,0,0,0);tr=locale.Translate
        self.Clip=AssetPickerInput(tr('audio.clip'),accepted_extensions={'.wav','.mp3','.flac','.ogg','.oga'},picker_title=tr('audio.clip'),search_placeholder=tr('properties.search_project_assets'),missing_label=tr('properties.missing_asset'))
        self.Clip.ConfigureAssets(assets);self.Clip.PickRequested.connect(self.Clip.OpenProjectPicker);self.Clip.ValueChanged.connect(self.SetClip);root.addWidget(self.Clip)
        self.Clip.AssetResolver=getattr(runtime,'ResolveAssetRecord',None)
        self.Bus=EnumInput();self.Bus.SetOptions(tuple((b['name'],b['id']) for b in buses));root.addWidget(QLabel(tr('audio.preview_bus')));root.addWidget(self.Bus)
        self.Volume=RangeInput(0,1,1,2);self.Volume.ValueChanged.connect(lambda value:runtime.AudioControl(self.Player.Voice,'volume',value) if self.Player else None);root.addWidget(QLabel(tr('audio.preview_volume')));root.addWidget(self.Volume)
        self.PlayerHost=QVBoxLayout();root.addLayout(self.PlayerHost)
    def SetClip(self,identity):
        if self.Player:self.Player.Stop();self.Player.Timer.stop();self.Player.deleteLater();self.Player=None
        info=self.Runtime.AudioClipInfo(identity)
        if not info:return
        outer=self
        class PreviewRuntime:
            def PreviewAudio(self,clip):
                voice=outer.Runtime.PreviewMixerAudio(outer.Identity,clip,outer.Bus.GetValue());outer.Runtime.AudioControl(voice,'volume',outer.Volume.GetValue());return voice
            def __getattr__(self,key):return getattr(outer.Runtime,key)
        self.Player=AudioPreview(PreviewRuntime(),info['uuid'],self.Locale,info['duration'],waveform_path=info.get('waveform_path'));self.PlayerHost.addWidget(self.Player)
