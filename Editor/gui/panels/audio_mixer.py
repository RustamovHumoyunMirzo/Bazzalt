"""Dockable, asset-backed mixer routing and channel-strip authoring."""
from copy import deepcopy
from math import log10
from pathlib import Path
import uuid
from PySide6.QtCore import Qt,QTimer,QSignalBlocker,Signal,QEvent,QSize,QRectF,QPoint
from PySide6.QtGui import QIcon,QPainter,QPalette,QPen,QLinearGradient
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QSplitter,QScrollArea,QTreeWidget,QTreeWidgetItem,QAbstractItemView,QLabel,QToolButton,QStyle,QSlider,QGroupBox
from ..widgets.fields import AssetPickerInput,BoolInput,EnumInput,FloatInput,StringInput

ZERO='00000000-0000-0000-0000-000000000000'

class MixerFader(QSlider):
    """Logarithmic console fader; painted gain scale, not a simulated signal meter."""
    def __init__(self,parent=None):
        super().__init__(Qt.Orientation.Vertical,parent);self.setRange(0,920);self.setMinimumSize(88,100);self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
    def sizeHint(self):return QSize(100,190)
    def _Y(self,value):return 14+(self.height()-28)*(1-value/920)
    def paintEvent(self,event):
        p=QPainter(self);p.setRenderHint(QPainter.RenderHint.Antialiasing);palette=self.palette();text=palette.color(QPalette.ColorRole.Text);muted=palette.color(QPalette.ColorRole.PlaceholderText);accent=palette.color(QPalette.ColorRole.Highlight)
        x=self.width()*.42;top=14;bottom=self.height()-14
        p.setPen(QPen(palette.color(QPalette.ColorRole.Dark),7));p.drawLine(int(x),top,int(x),bottom)
        p.setPen(QPen(accent,2));p.drawLine(int(x),int(self._Y(self.value())),int(x),bottom)
        font=p.font();font.setPointSizeF(max(7,font.pointSizeF()-1));p.setFont(font)
        for db in (12,6,0,-6,-12,-24,-48,-80):
            y=self._Y((db+80)*10);p.setPen(QPen(text if db==0 else muted,1));p.drawLine(int(x+12),int(y),int(x+19),int(y));p.drawText(QRectF(x+23,y-7,35,14),Qt.AlignmentFlag.AlignLeft|Qt.AlignmentFlag.AlignVCenter,'−∞' if db==-80 else str(db))
        cap=QRectF(x-15,self._Y(self.value())-7,30,14);gradient=QLinearGradient(cap.topLeft(),cap.bottomLeft());gradient.setColorAt(0,palette.color(QPalette.ColorRole.Light));gradient.setColorAt(1,palette.color(QPalette.ColorRole.Button));p.setBrush(gradient);p.setPen(QPen(accent if self.hasFocus() else palette.color(QPalette.ColorRole.Mid),1));p.drawRoundedRect(cap,2,2);p.setPen(QPen(text,1));p.drawLine(cap.topLeft().toPoint()+QPoint(4,7),cap.topRight().toPoint()+QPoint(-4,7))
    def _SetAt(self,event):self.setValue(round(max(0,min(920,(1-(event.position().y()-14)/max(1,self.height()-28))*920))))
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self.setFocus();self._SetAt(event);event.accept()
        else:super().mousePressEvent(event)
    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.LeftButton:self._SetAt(event);event.accept()
        else:super().mouseMoveEvent(event)
    def mouseDoubleClickEvent(self,event):self.setValue(800);event.accept()

class RoutingTree(QTreeWidget):
    RouteRequested=Signal(str,str)
    def dropEvent(self,event):
        source=self.currentItem();target=self.itemAt(event.position().toPoint())
        if event.source() is not self or source is None or target is None:event.ignore();return
        self.RouteRequested.emit(source.data(0,Qt.ItemDataRole.UserRole),target.data(0,Qt.ItemDataRole.UserRole));event.accept()

class AudioMixerPanel(QWidget):
    Saved=Signal(str)
    def __init__(self,runtime,localization,resources=None):
        super().__init__();self.Runtime=runtime;self.Locale=localization;self.Resources=resources;self.AssetProvider=lambda:[]
        self.Identity='';self.Path=None;self.Buses=[];self.Pending=False;self.Stamp=None;self.Strips={}
        self.SaveTimer=QTimer(self);self.SaveTimer.setSingleShot(True);self.SaveTimer.setInterval(250);self.SaveTimer.timeout.connect(self.Flush)
        self.SyncTimer=QTimer(self);self.SyncTimer.setInterval(1500);self.SyncTimer.timeout.connect(self.CheckExternal);self.SyncTimer.start()
        root=QVBoxLayout(self);root.setContentsMargins(6,6,6,6);root.setSpacing(6);bar=QHBoxLayout();root.addLayout(bar)
        tr=localization.Translate
        self.Picker=AssetPickerInput(tr('audio.choose_mixer'),accepted_extensions={'.bmixer'},picker_title=tr('audio.choose_mixer'),search_placeholder=tr('properties.search_project_assets'),missing_label=tr('properties.missing_asset'))
        self.Picker.PickRequested.connect(self.Choose);self.Picker.ValueChanged.connect(self.Open);root.insertWidget(0,self.Picker)
        self.Add=QToolButton();self.Remove=QToolButton();self.Reload=QToolButton();self.Save=QToolButton()
        for button,key in ((self.Add,'add_bus'),(self.Remove,'remove_bus'),(self.Reload,'reload'),(self.Save,'save')):button.setToolTip(tr('audio.'+key));button.setAccessibleName(tr('audio.'+key));button.setIconSize(QSize(16,16));bar.addWidget(button)
        bar.addStretch();self.UpdateIcons()
        self.Add.clicked.connect(self.AddBus);self.Remove.clicked.connect(self.RemoveSelected);self.Reload.clicked.connect(self.ReloadAsset);self.Save.clicked.connect(self.Flush)
        self.Error=QLabel();self.Error.setWordWrap(True);root.addWidget(self.Error)
        self.Empty=QLabel(tr('audio.mixer_empty'));self.Empty.setAlignment(Qt.AlignmentFlag.AlignCenter);self.Empty.setWordWrap(True);root.addWidget(self.Empty)
        self.Splitter=QSplitter();root.addWidget(self.Splitter,1)
        self.Tree=RoutingTree();self.Tree.setHeaderHidden(True);self.Tree.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection);self.Tree.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove);self.Tree.setDefaultDropAction(Qt.DropAction.MoveAction);self.Tree.setMinimumSize(80,55)
        self.Tree.itemSelectionChanged.connect(self.SelectionChanged);self.Tree.RouteRequested.connect(self.Route);self.Splitter.addWidget(self.Tree)
        self.Scroll=QScrollArea();self.Scroll.setWidgetResizable(True);self.Scroll.setFrameShape(QScrollArea.Shape.NoFrame);self.Channels=QWidget();self.Row=QHBoxLayout(self.Channels);self.Row.setContentsMargins(4,0,4,0);self.Row.setSpacing(8);self.Scroll.setWidget(self.Channels);self.Splitter.addWidget(self.Scroll);self.Splitter.setStretchFactor(1,1);self.Splitter.setSizes([180,650])
        localization.LocaleChanged.connect(self.Retranslate);self.Rebuild()
    def resizeEvent(self,event):
        super().resizeEvent(event)
        if hasattr(self,'Splitter'):
            orientation=Qt.Orientation.Vertical if self.width()<480 else Qt.Orientation.Horizontal
            if self.Splitter.orientation()!=orientation:
                self.Splitter.setOrientation(orientation);self.Splitter.setSizes([80,max(150,self.height()-80)] if orientation==Qt.Orientation.Vertical else [150,max(200,self.width()-150)])
    def Retranslate(self,*_):
        tr=self.Locale.Translate
        for button,key in ((self.Add,'add_bus'),(self.Remove,'remove_bus'),(self.Reload,'reload'),(self.Save,'save')):button.setToolTip(tr('audio.'+key));button.setAccessibleName(tr('audio.'+key))
        self.Empty.setText(tr('audio.mixer_empty'));self.Rebuild()
    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type()==QEvent.Type.PaletteChange and hasattr(self,'Add'):self.UpdateIcons()
    def UpdateIcons(self):
        for button,standard in ((self.Add,QStyle.StandardPixmap.SP_FileDialogNewFolder),(self.Remove,QStyle.StandardPixmap.SP_TrashIcon),(self.Reload,QStyle.StandardPixmap.SP_BrowserReload),(self.Save,QStyle.StandardPixmap.SP_DialogSaveButton)):
            pixmap=self.style().standardIcon(standard).pixmap(16,16);painter=QPainter(pixmap);painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),self.palette().color(QPalette.ColorRole.ButtonText));painter.end();button.setIcon(QIcon(pixmap))
    def Choose(self):
        self.Picker.ConfigureAssets(self.AssetProvider());self.Picker.OpenProjectPicker()
    def _Stamp(self):
        try:s=self.Path.stat();return (s.st_mtime_ns,s.st_size)
        except (OSError,AttributeError):return None
    def Open(self,identity):
        if not identity:
            if self.Flush():self.Identity='';self.Path=None;self.Buses=[];self.Rebuild()
            return
        if not self.Flush():self._ResetPicker();return False
        info=self.Runtime.AssetInfo(str(identity))
        if not info or info.get('importer')!='Bazzalt.AudioMixer' or info.get('error'):
            self.Error.setText(self.Locale.Translate('audio.mixer_unavailable'));self._ResetPicker();return False
        self.Identity=info['uuid'];self.Path=Path(info['path']);self.Stamp=self._Stamp();self.Buses=deepcopy(self.Runtime.AudioBuses(self.Identity));self.Pending=False;self.Error.clear();self._ResetPicker();self.Rebuild();return True
    def _ResetPicker(self):
        blocker=QSignalBlocker(self.Picker);self.Picker.ConfigureAssets(self.AssetProvider());self.Picker.SetValue(self.Identity or None,self.Path.stem if self.Path else None);del blocker
    def ReloadAsset(self):
        if not self.Identity:return
        self.SaveTimer.stop();self.Pending=False
        if self.Runtime.RefreshAudioMixer(self.Identity):self.Open(self.Identity)
        else:self.Error.setText(self.Runtime.LastError())
    def Flush(self):
        self.SaveTimer.stop()
        if not self.Pending:return True
        if self._Stamp()!=self.Stamp:
            self.Error.setText(self.Locale.Translate('audio.mixer_conflict'));return False
        if not self.Runtime.SaveAudioMixer(self.Identity,self.Buses):
            self.Error.setText(self.Runtime.LastError());return False
        self.Pending=False;self.Stamp=self._Stamp();self.Save.setEnabled(False);self.Error.clear();self.Saved.emit(str(self.Path));return True
    def CheckExternal(self):
        if not self.Identity or not self.isVisible():return
        info=self.Runtime.AssetInfo(self.Identity)
        if info and Path(info['path'])!=self.Path:
            self.Path=Path(info['path']);self._ResetPicker()
        if self._Stamp()!=self.Stamp:
            if self.Pending:self.SaveTimer.stop();self.Error.setText(self.Locale.Translate('audio.mixer_conflict'))
            else:self.ReloadAsset()
    def _Bus(self,identity):return next((b for b in self.Buses if b['id']==identity),None)
    def _Descendants(self,identity):
        result={identity}
        for _ in self.Buses:result.update(b['id'] for b in self.Buses if b['id']!=ZERO and b['parent'] in result)
        return result
    def Edit(self,identity,key,value):
        bus=self._Bus(identity)
        if bus is None:return
        if key=='name' and (not value.strip() or len(value.encode('utf-8'))>128):return
        if key=='parent' and (identity==ZERO or value in self._Descendants(identity) or self._Bus(value) is None):return
        if bus[key]==value:return
        bus[key]=value;self.Pending=True;self.Save.setEnabled(True);self.SaveTimer.start()
        if key in ('parent','name'):self.UpdateRouting()
    def Route(self,identity,parent):
        self.Edit(identity,'parent',parent)
        if identity in self.Strips and 'parent' in self.Strips[identity]:
            combo=self.Strips[identity]['parent'];blocker=QSignalBlocker(combo);combo.SetValue(self._Bus(identity)['parent']);del blocker
    def AddBus(self):
        if not self.Identity or len(self.Buses)>=256:return
        parent=self.Selected() or ZERO;identity=str(uuid.uuid4());self.Buses.append(dict(id=identity,parent=parent,name=self.Locale.Translate('audio.new_bus'),volume=1.,pitch=1.,muted=False,solo=False,paused=False));self.Pending=True;self.SaveTimer.start();self.Rebuild(identity)
    def RemoveSelected(self):
        identity=self.Selected();bus=self._Bus(identity)
        if bus is None or identity==ZERO:return
        for child in self.Buses:
            if child['parent']==identity:child['parent']=bus['parent']
        self.Buses.remove(bus);self.Pending=True;self.SaveTimer.start();self.Rebuild(bus['parent'])
    def Selected(self):
        item=self.Tree.currentItem();return item.data(0,Qt.ItemDataRole.UserRole) if item else None
    def UpdateRouting(self,selected=None):
        selected=selected or self.Selected();blocker=QSignalBlocker(self.Tree);self.Tree.clear();items={};pending=list(self.Buses)
        while pending:
            progress=False
            for bus in list(pending):
                if bus['id']!=ZERO and bus['parent'] not in items:continue
                item=QTreeWidgetItem([bus['name']]);item.setData(0,Qt.ItemDataRole.UserRole,bus['id']);item.setFlags(item.flags()|Qt.ItemFlag.ItemIsDragEnabled|Qt.ItemFlag.ItemIsDropEnabled)
                if bus['id']==ZERO:self.Tree.addTopLevelItem(item)
                else:items[bus['parent']].addChild(item)
                items[bus['id']]=item;pending.remove(bus);progress=True
            if not progress:break
        self.Tree.expandAll()
        if selected in items:self.Tree.setCurrentItem(items[selected])
        del blocker;self.SelectionChanged()
        for identity,strip in self.Strips.items():
            if 'parent' not in strip:continue
            combo=strip['parent'];blocked=QSignalBlocker(combo);combo.SetOptions(tuple((b['name'],b['id']) for b in self.Buses if b['id'] not in self._Descendants(identity)));combo.SetValue(self._Bus(identity)['parent']);del blocked
    def SelectionChanged(self):
        selected=self.Selected();self.Remove.setEnabled(bool(selected and selected!=ZERO))
        for identity,strip in self.Strips.items():
            group=strip['group'];group.setProperty('fieldState','modified' if identity==selected else 'default');group.style().unpolish(group);group.style().polish(group)
    def Rebuild(self,selected=None):
        self.Strips={}
        while self.Row.count():
            item=self.Row.takeAt(0)
            if item.widget():item.widget().deleteLater()
        tr=self.Locale.Translate
        for bus in sorted(self.Buses,key=lambda b:(b['id']!=ZERO,b['name'].casefold())):
            identity=bus['id'];group=QGroupBox();group.setObjectName('AudioMixerChannel');group.setFixedWidth(128);layout=QVBoxLayout(group);layout.setContentsMargins(8,8,8,8);layout.setSpacing(5)
            name=StringInput(bus['name']);name.editingFinished.connect(lambda b=identity,f=name:self.Edit(b,'name',f.text().strip()));layout.addWidget(name)
            strip={'group':group};self.Strips[identity]=strip
            if identity!=ZERO:
                parent=EnumInput();parent.SetOptions(tuple((b['name'],b['id']) for b in self.Buses if b['id'] not in self._Descendants(identity)));parent.SetValue(bus['parent']);parent.currentIndexChanged.connect(lambda _,b=identity,f=parent:self.Edit(b,'parent',f.GetValue()));layout.addWidget(QLabel(tr('audio.bus_parent')));layout.addWidget(parent);strip['parent']=parent
            else:layout.addWidget(QLabel(tr('audio.master_output')))
            flags=QHBoxLayout();layout.addLayout(flags)
            for key in ('muted','solo','paused'):
                field=QToolButton();field.setObjectName('MixerChannelSwitch');field.setCheckable(True);field.setChecked(bus[key]);field.setText(tr('audio.short_'+key));field.setToolTip(tr('audio.bus_'+key));field.toggled.connect(lambda value,b=identity,k=key:self.Edit(b,k,value));flags.addWidget(field)
            fader=MixerFader();fader.setValue(round((max(-80.,20*log10(max(.0001,bus['volume'])))+80)*10));layout.addWidget(fader,1)
            db=QLabel();db.setAlignment(Qt.AlignmentFlag.AlignCenter);layout.addWidget(db)
            def show_db(value,label=db):label.setText(('−∞' if value==0 else f'{value/10-80:+.1f}')+' dB')
            show_db(fader.value());fader.valueChanged.connect(show_db)
            gain=FloatInput(0,4,bus['volume'],3);layout.addWidget(QLabel(tr('audio.bus_volume')));layout.addWidget(gain)
            def gain_changed(value,b=identity,slider=fader):
                blocker=QSignalBlocker(slider);slider.setValue(round((max(-80.,20*log10(max(.0001,value)))+80)*10));del blocker;show_db(slider.value(),self.Strips[b]['db']);self.Edit(b,'volume',value)
            def fader_changed(value,b=identity,field=gain):
                gain_value=0. if value==0 else min(4.,10**((value/10-80)/20));blocker=QSignalBlocker(field);field.setValue(gain_value);del blocker;self.Edit(b,'volume',gain_value)
            gain.valueChanged.connect(gain_changed);fader.valueChanged.connect(fader_changed)
            strip.update(fader=fader,db=db,gain=gain)
            pitch=FloatInput(.25,4,bus['pitch'],2);pitch.valueChanged.connect(lambda value,b=identity:self.Edit(b,'pitch',value));layout.addWidget(QLabel(tr('audio.bus_pitch')));layout.addWidget(pitch);self.Row.addWidget(group)
        self.Row.addStretch();self.UpdateRouting(selected);self.Splitter.setVisible(bool(self.Identity));self.Empty.setVisible(not self.Identity)
        self.Add.setEnabled(bool(self.Identity) and len(self.Buses)<256);self.Save.setEnabled(self.Pending);self.Reload.setEnabled(bool(self.Identity))
