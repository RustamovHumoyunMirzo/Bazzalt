"""Asset-backed integration of the mixer designed in testamixer.py."""
from copy import deepcopy
import json
import math
from pathlib import Path
import uuid
from PySide6.QtCore import Qt,QTimer,QSignalBlocker,Signal
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QLabel,QToolButton,QListWidgetItem
from ..widgets.fields import AssetPickerInput
from .mixer_controls import AudioMixer,MASTER,ROLE_NAME,DbToLinear,LinearToDb

ZERO='00000000-0000-0000-0000-000000000000'

class AudioMixerPanel(QWidget):
    Saved=Signal(str)
    def __init__(self,runtime,localization,resources=None,theme_manager=None):
        super().__init__();self.Runtime=runtime;self.Locale=localization;self.Resources=resources;self.AssetProvider=lambda:[]
        self.Identity='';self.Path=None;self.Pending=False;self.Stamp=None;self.BusIds={};self.EffectIds={};self.Original={};self.Loading=False
        root=QVBoxLayout(self);root.setContentsMargins(0,0,0,0);root.setSpacing(3)
        self._root=root;self._controls=None;self._themes=theme_manager
        bar=QHBoxLayout();root.addLayout(bar);tr=self.Locale.Translate
        self.Picker=AssetPickerInput(tr('audio.choose_mixer'),accepted_extensions={'.bmixer'},picker_title=tr('audio.choose_mixer'),search_placeholder=tr('properties.search_project_assets'),missing_label=tr('properties.missing_asset'))
        self.Picker.PickRequested.connect(self.Choose);self.Picker.ValueChanged.connect(self.Open);bar.addWidget(self.Picker,1)
        self.Reload=QToolButton();self.Save=QToolButton()
        for button,key in ((self.Reload,'reload'),(self.Save,'save')):button.setText(tr('audio.'+key));bar.addWidget(button)
        self.Reload.clicked.connect(self.ReloadAsset);self.Save.clicked.connect(self.Flush)
        self.Error=QLabel();self.Error.setWordWrap(True);root.addWidget(self.Error)
        self.SaveTimer=QTimer(self);self.SaveTimer.setSingleShot(True);self.SaveTimer.setInterval(250);self.SaveTimer.timeout.connect(self.Flush)
        self.SyncTimer=QTimer(self);self.SyncTimer.setInterval(1500);self.SyncTimer.timeout.connect(self.CheckExternal);self.SyncTimer.start()
        self.LevelTimer=QTimer(self);self.LevelTimer.setInterval(50);self.LevelTimer.timeout.connect(self.PollLevels)
        localization.LocaleChanged.connect(self.Retranslate);self.Save.setEnabled(False)
    @property
    def Controls(self):
        # Dock registration reparents panels repeatedly. Do not construct an
        # unused mixer tree/rack during every editor startup/rebuild.
        if self._controls is None:
            controls=AudioMixer(self);self._controls=controls;self._root.addWidget(controls,1)
            if self._themes:controls.BindThemeManager(self._themes)
            controls.AssetPickRequested.connect(self.Choose);controls._mixer_list.itemClicked.disconnect();controls._mixer_list.itemClicked.connect(lambda item:self.Open(item.data(ROLE_NAME)))
            for name in ('GroupAdded','GroupRemoved','GroupReparented','GroupColorChanged','GroupVisibilityChanged','GroupLockChanged','VolumeChanged','MuteChanged','SoloChanged','BypassChanged','EffectAdded','EffectRemoved','EffectMoved','EffectEnabledChanged','EffectParameterChanged','SnapshotCreated','SnapshotRemoved','SnapshotRenamed','SnapshotSwitched','SnapshotFavoriteChanged','ViewCreated','ViewRemoved','ViewRenamed','ViewSwitched'):
                getattr(controls,name).connect(self.Changed)
            controls.GroupRenamed.connect(self.Renamed);controls.GroupRemoved.connect(lambda name:self.BusIds.pop(name,None));controls.Retranslate(self.Locale)
            self._SetAssetEnabled(bool(self.Identity))
        return self._controls
    def _SetAssetEnabled(self,enabled):
        controls=self._controls
        if controls is None:return
        for widget in (controls._strip_host,controls._tree,controls._snapshot_list,controls._view_list):widget.setEnabled(enabled)
        for button in controls._sidebar.findChildren(QToolButton):
            if button.objectName()=='MixerSectionAdd' and button is not controls._mixer_section.AddButton:button.setEnabled(enabled)
    def Retranslate(self,*_):
        self.Reload.setText(self.Locale.Translate('audio.reload'));self.Save.setText(self.Locale.Translate('audio.save'))
        if self._controls:self._controls.Retranslate(self.Locale)
    def Choose(self):self.Picker.ConfigureAssets(self.AssetProvider());self.Picker.OpenProjectPicker()
    def _Stamp(self):
        try:s=self.Path.stat();return s.st_mtime_ns,s.st_size
        except (OSError,AttributeError):return None
    def _ResetPicker(self):
        blocker=QSignalBlocker(self.Picker);self.Picker.ConfigureAssets(self.AssetProvider());self.Picker.SetValue(self.Identity or None,self.Path.stem if self.Path else None);del blocker
    def Open(self,identity):
        if not self.Flush():self._ResetPicker();self.RefreshMixerList();return False
        if not identity:
            self.Identity='';self.Path=None
            self._SetAssetEnabled(False)
            return True
        info=self.Runtime.AssetInfo(str(identity))
        if not info or info.get('importer')!='Bazzalt.AudioMixer' or info.get('error'):self.Error.setText(self.Locale.Translate('audio.mixer_unavailable'));return False
        buses=self.Runtime.AudioBuses(info['uuid'])
        if not buses:self.Error.setText(self.Locale.Translate('audio.mixer_unavailable'));return False
        try:
            raw=self.Runtime.AudioMixerEditorState(info['uuid']);state=json.loads(raw) if raw else None
            if state is not None and (not isinstance(state,dict) or not isinstance(state.get('Groups'),list)):raise ValueError('Invalid mixer editor state')
            ids=state.get('BusIds',{}) if state else {};reverse={v:k for k,v in ids.items()};names={};taken=set()
            for bus in buses:
                name=MASTER if bus['id']==ZERO else reverse.get(bus['id'],bus['name']);base=name;count=1
                while name in taken:name=f'{base} {count}';count+=1
                names[bus['id']]=name;taken.add(name)
            groups=[];old={g['Name']:g for g in state['Groups']} if state else {};effect_ids=state.get('EffectIds',{}) if state else {};fx_reverse={v:k for k,v in effect_ids.items()}
            for bus in buses:
                name=names[bus['id']];g=deepcopy(old.get(name,{}));g.update(Name=name,Parent=names[bus['parent']] if bus['id']!=ZERO else None,Volume=LinearToDb(bus['volume']),Muted=bus['muted'],Soloed=bus['solo'],Bypass=bus.get('bypass',False),Children=[names[b['id']] for b in buses if b['id']!=ZERO and b['parent']==bus['id']])
                g['Effects']=[dict(Id=fx_reverse.get(e['id'],e['id']),Type=e['type'],Enabled=e['enabled'],Parameters=e['params']) for e in bus.get('effects',[])];groups.append(g)
            state=state or {};state.update(Name=info['uuid'],Groups=groups)
            self.Loading=True;self.Controls._mixers={};self.Controls.Deserialize(state)
            self.Identity=info['uuid'];self.Path=Path(info['path']);self.Stamp=self._Stamp();self.BusIds={names[b['id']]:b['id'] for b in buses};self.EffectIds=effect_ids;self.Original={b['id']:b for b in buses};self.Pending=False;self.Error.clear();self._SetAssetEnabled(True);self._ResetPicker();self.RefreshMixerList();self.Retranslate();return True
        except (ValueError,TypeError,KeyError,RecursionError) as error:self.Error.setText(str(error));return False
        finally:self.Loading=False
    def RefreshMixerList(self):
        widget=self.Controls._mixer_list;blocked=QSignalBlocker(widget);widget.clear();assets=[a for a in self.AssetProvider() if a.get('extension')=='.bmixer']
        if self.Path and not any(a['uuid']==self.Identity for a in assets):assets.append(dict(uuid=self.Identity,name=self.Path.name))
        for asset in assets:
            item=QListWidgetItem(Path(asset['name']).stem);item.setData(ROLE_NAME,asset['uuid']);widget.addItem(item)
            if self.Resources:item.setIcon(self.Resources.Icon('icons/abrowser/audiomixer.svg'))
            if asset['uuid']==self.Identity:widget.setCurrentItem(item)
        del blocked
    def Renamed(self,old,new):
        if old in self.BusIds:self.BusIds[new]=self.BusIds.pop(old)
        self.Changed()
    def Changed(self,*_):
        if self.Loading or not self.Identity:return
        self.Pending=True;self.Save.setEnabled(True);self.SaveTimer.start()
    def _Payload(self):
        values=[]
        for name in self.Controls.GetGroups():self.BusIds.setdefault(name,ZERO if name==MASTER else str(uuid.uuid4()))
        for name,g in self.Controls._mixer.groups.items():
            identity=self.BusIds[name];bus=deepcopy(self.Original.get(identity,dict(pitch=1.,paused=False)))
            bus.update(id=identity,parent=self.BusIds[g.parent] if g.parent else ZERO,name=name,volume=DbToLinear(g.volume),muted=g.muted,solo=g.soloed,bypass=g.bypass,effects=[])
            for e in g.effects:
                effect_id=self.EffectIds.setdefault(e.id,str(uuid.uuid4()));bus['effects'].append(dict(id=effect_id,type=e.type,enabled=e.enabled,params=e.params))
            values.append(bus)
        state=self.Controls.Serialize();state['BusIds']={name:self.BusIds[name] for name in self.Controls.GetGroups()};state['EffectIds']=self.EffectIds
        return values,json.dumps(state,ensure_ascii=False,separators=(',',':'))
    def Flush(self):
        self.SaveTimer.stop()
        if not self.Pending:return True
        if self._Stamp()!=self.Stamp:self.Error.setText(self.Locale.Translate('audio.mixer_conflict'));return False
        try:buses,state=self._Payload()
        except (ValueError,KeyError) as error:self.Error.setText(str(error));return False
        if not self.Runtime.SaveAudioMixer(self.Identity,buses,state):self.Error.setText(self.Runtime.LastError());return False
        self.Pending=False;self.Stamp=self._Stamp();self.Save.setEnabled(False);self.Error.clear();self.Saved.emit(str(self.Path));return True
    def ReloadAsset(self):
        if not self.Identity:return
        self.SaveTimer.stop();self.Pending=False
        if self.Runtime.RefreshAudioMixer(self.Identity):self.Open(self.Identity)
        else:self.Error.setText(self.Runtime.LastError())
    def CheckExternal(self):
        if not self.Identity or not self.isVisible():return
        info=self.Runtime.AssetInfo(self.Identity)
        if info and Path(info['path'])!=self.Path:self.Path=Path(info['path']);self._ResetPicker();self.RefreshMixerList()
        if self._Stamp()!=self.Stamp:
            if self.Pending:self.SaveTimer.stop();self.Error.setText(self.Locale.Translate('audio.mixer_conflict'))
            else:self.ReloadAsset()
    def PollLevels(self):
        if not self.Identity or not self.isVisible():return
        levels=self.Runtime.AudioMixerLevels(self.Identity)
        for name,identity in self.BusIds.items():
            values=levels.get(identity,(0,0));self.Controls.SetMeterLevels(name,*(max(-80,20*math.log10(max(1e-8,v))) for v in values))
    def showEvent(self,event):
        super().showEvent(event);self.LevelTimer.start();QTimer.singleShot(0,self,self._Shown)
    def _Shown(self):
        if self.isVisible() and self.window().isVisible():self.Controls;self.RefreshMixerList()
    def hideEvent(self,event):self.LevelTimer.stop();super().hideEvent(event)
