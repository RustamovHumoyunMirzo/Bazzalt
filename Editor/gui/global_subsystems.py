"""Modal project subsystem registry, not a docking panel."""
import json
from pathlib import Path
from PySide6.QtCore import Qt,QTimer
from PySide6.QtWidgets import QDialog,QVBoxLayout,QHBoxLayout,QTreeWidget,QTreeWidgetItem,QPushButton,QLabel,QDialogButtonBox
from .widgets.fields import AssetPickerInput,EnumInput
from ..subsystems import Registrations,Sources,KEY,LEVELS

class GlobalSubsystemsDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate;self.Info=editor.Runtime.ProjectInfo()
        self.setWindowTitle(self.Tr('subsystems.title'));self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(680,480)
        root=QVBoxLayout(self);self.Tree=QTreeWidget();self.Tree.setHeaderLabels([self.Tr('subsystems.script'),self.Tr('subsystems.level')]);root.addWidget(self.Tree,1)
        self.Error=QLabel();self.Error.setWordWrap(True);root.addWidget(self.Error)
        row=QHBoxLayout();self.Picker=AssetPickerInput(self.Tr('subsystems.select'),accepted_extensions=('.cpp',),picker_title=self.Tr('subsystems.select'),search_placeholder=self.Tr('properties.search_project_assets'),missing_label=self.Tr('properties.missing_asset'))
        self.Picker.ConfigureAssets(editor.Controller._ProjectAssets());self.Picker.AssetResolver=editor.Runtime.ResolveAssetRecord;self.Picker.PickRequested.connect(self.Picker.OpenProjectPicker)
        self.Level=EnumInput();self.Level.SetOptions([(self.Tr('subsystems.level.'+name),index) for index,name in enumerate(LEVELS)]);self.Level.SetValue(4)
        add=QPushButton(self.Tr('subsystems.add'));add.clicked.connect(self._Add);remove=QPushButton(self.Tr('subsystems.remove'));remove.clicked.connect(self._Remove)
        row.addWidget(self.Picker,1);row.addWidget(self.Level);row.addWidget(add);row.addWidget(remove);root.addLayout(row)
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Apply|QDialogButtonBox.StandardButton.Close);buttons.button(QDialogButtonBox.StandardButton.Apply).clicked.connect(self._Apply);buttons.rejected.connect(self.reject);root.addWidget(buttons)
        self.Values=[]
        try:self.Values=Registrations(self.Info.get('properties',{}))
        except (ValueError,TypeError) as error:self.Error.setText(str(error))
        self._Refresh()
        enabled=bool(self.Info) and not editor.Runtime.IsPlaying()
        for widget in (self.Tree,self.Picker,self.Level,add,remove,buttons.button(QDialogButtonBox.StandardButton.Apply)):widget.setEnabled(enabled)
        if not enabled:self.Error.setText(self.Tr('subsystems.stop_playing' if self.Info else 'subsystems.no_project'))
    def _Refresh(self):
        self.Tree.clear();groups=[]
        for name in LEVELS:
            group=QTreeWidgetItem([self.Tr('subsystems.level.'+name),'']);group.setFlags(Qt.ItemFlag.ItemIsEnabled);self.Tree.addTopLevelItem(group);group.setExpanded(True);groups.append(group)
        for index,value in enumerate(self.Values):
            record=self.Editor.Runtime.ResolveAssetRecord(value['asset']);item=QTreeWidgetItem([record['name'] if record else self.Tr('properties.missing_asset'),self.Tr('subsystems.level.'+LEVELS[value['level']])]);item.setData(0,Qt.ItemDataRole.UserRole,index)
            if record:item.setToolTip(0,record['path'])
            groups[value['level']].addChild(item)
            level=EnumInput();level.SetOptions([(self.Tr('subsystems.level.'+name),i) for i,name in enumerate(LEVELS)]);level.SetValue(value['level']);self.Tree.setItemWidget(item,1,level)
            level.currentIndexChanged.connect(lambda _,i=index,field=level:self._ChangeLevel(i,field.GetValue()))
        self.Tree.setColumnWidth(0,420)
    def _Validate(self,values):
        properties=dict(self.Info.get('properties',{}));properties[KEY]=json.dumps(values);values=Registrations(properties)
        Sources(Path(self.Info['project_path']),values);return values
    def _ChangeLevel(self,index,level):
        self.Values[index]['level']=int(level);QTimer.singleShot(0,self,self._Refresh)
    def _Add(self):
        identity=self.Picker.GetValue()
        if not identity:return
        try:self.Values=self._Validate([*self.Values,dict(asset=identity,level=int(self.Level.GetValue()))]);self.Error.clear();self._Refresh();self.Picker.Clear()
        except (ValueError,OSError,TypeError) as error:self.Error.setText(str(error))
    def _Remove(self):
        item=self.Tree.currentItem();index=item.data(0,Qt.ItemDataRole.UserRole) if item else None
        if index is not None:self.Values.pop(index);self._Refresh()
    def _Apply(self):
        try:
            values=self._Validate(self.Values);info=self.Editor.Runtime.ProjectInfo();properties=dict(info.get('properties',{}));properties[KEY]=json.dumps(values);properties.pop('application.script',None)
            if not self.Editor.Runtime.UpdateProjectInfo(info['name'],properties):raise ValueError(self.Editor.Runtime.LastError())
            self.Info=self.Editor.Runtime.ProjectInfo();self.accept()
        except (ValueError,OSError,TypeError) as error:self.Error.setText(str(error))
