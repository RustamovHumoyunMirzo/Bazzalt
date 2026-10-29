"""LOD authoring uses ordinary themed fields and entity references, never files."""
from copy import deepcopy
from PySide6.QtCore import Signal,Qt,QSignalBlocker
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QDialog,QListWidget,QListWidgetItem,QPushButton,QLabel
from .widgets import FloatInput,ObjectPickerInput,StringInput

class LODLevelsInput(QWidget):
    ValueChanged=Signal(object)
    def __init__(self,controller,entity,value,parent=None):
        super().__init__(parent);self.Controller=controller;self.Entity=entity;self.Tr=controller.Window.Localization.Translate
        self.Layout=QVBoxLayout(self);self.Layout.setContentsMargins(0,0,0,0);self.Layout.setSpacing(4);self._value=[];self.SetValue(value)
    def GetValue(self):
        value=deepcopy(self._value)
        for row in value:row['Roots']=[v for v in row['Roots'] if v]
        return value
    def SetValue(self,value):
        self._value=deepcopy(list(value));self._Rebuild()
    def _Button(self,key,callback):
        button=QPushButton(self.Tr(key));button.clicked.connect(callback);return button
    def _Emit(self):self.ValueChanged.emit(self.GetValue())
    def _ChangeHeight(self,index,value):self._value[index]['ScreenHeight']=value;self._Emit()
    def _ChangeRoot(self,index,root,value,field):
        self._value[index]['Roots'][root]=value or '';field.Display.setText(self.Controller.Runtime.EntityDetails(value).get('name',self.Tr('lod.missing_root')) if value else '');self._Emit()
    def _IsChild(self,identity):
        seen=set();current=str(identity)
        while current and current not in seen:
            if current==self.Entity:return identity!=self.Entity
            seen.add(current);current=str(self.Controller.Runtime.EntityDetails(current).get('parent') or '')
        return False
    def _Pick(self,field):
        dialog=QDialog(self);dialog.setWindowTitle(self.Tr('lod.pick_root'));layout=QVBoxLayout(dialog);search=StringInput();search.setPlaceholderText(self.Tr('lod.search'));layout.addWidget(search);items=QListWidget(dialog);layout.addWidget(items)
        for scene in self.Controller.Runtime.LoadedScenes():
            for entity in scene.get('entities',[]):
                identity=str(entity['uuid'])
                if self._IsChild(identity):
                    row=QListWidgetItem(entity.get('name',''));row.setData(Qt.ItemDataRole.UserRole,identity);items.addItem(row)
        search.textChanged.connect(lambda text:[items.item(i).setHidden(text.casefold() not in items.item(i).text().casefold()) for i in range(items.count())])
        def accept(item):
            identity=item.data(Qt.ItemDataRole.UserRole);field.SetValue(identity,item.text());dialog.accept()
        items.itemDoubleClicked.connect(accept);dialog.exec();dialog.deleteLater()
    def _AddRoot(self,index):
        self._value[index]['Roots'].append('');self._Rebuild()
    def _RemoveRoot(self,index,root):
        del self._value[index]['Roots'][root];self._Emit();self._Rebuild()
    def _AddLevel(self):
        if len(self._value)>=8:return
        last=self._value[-1]['ScreenHeight'] if self._value else 1
        if last<=.0001:return
        self._value.append({'ScreenHeight':last*.5,'Roots':[]});self._Emit();self._Rebuild()
    def _RemoveLevel(self,index):
        if len(self._value)<=1:return
        del self._value[index];self._Emit();self._Rebuild()
    def _Rebuild(self):
        while self.Layout.count():
            item=self.Layout.takeAt(0)
            if item.widget():item.widget().hide();item.widget().deleteLater()
            elif item.layout():
                row=item.layout()
                while row.count():
                    child=row.takeAt(0)
                    if child.widget():child.widget().hide();child.widget().deleteLater()
                row.deleteLater()
        for index,level in enumerate(self._value):
            row=QHBoxLayout();row.setContentsMargins(0,0,0,0);row.setSpacing(4)
            row.addWidget(QLabel(self.Tr('lod.level',index=index)))
            height=FloatInput(minimum=0,maximum=1,value=level['ScreenHeight'],decimals=4);height.setToolTip(self.Tr('lod.screen_height'));height.valueChanged.connect(lambda value,i=index:self._ChangeHeight(i,value));row.addWidget(height)
            remove=self._Button('lod.remove_level',lambda checked=False,i=index:self._RemoveLevel(i));remove.setEnabled(len(self._value)>1);row.addWidget(remove);self.Layout.addLayout(row)
            for root,identity in enumerate(level['Roots']):
                field=ObjectPickerInput(validator=lambda value:self._IsChild(value));details=self.Controller.Runtime.EntityDetails(identity) if identity else {};field.SetValue(identity or None,details.get('name',''))
                field.ValueChanged.connect(lambda value,i=index,r=root,f=field:self._ChangeRoot(i,r,value,f));field.PickRequested.connect(lambda f=field:self._Pick(f))
                roots=QHBoxLayout();roots.setSpacing(4);roots.addWidget(field);roots.addWidget(self._Button('lod.remove_root',lambda checked=False,i=index,r=root:self._RemoveRoot(i,r)));self.Layout.addLayout(roots)
            self.Layout.addWidget(self._Button('lod.add_root',lambda checked=False,i=index:self._AddRoot(i)))
        section=self.Controller.Window.Properties._sections.get('runtime.LOD')
        forced=section._fields.get('Forced Level') if section else None
        if forced is not None:
            data=self.Controller.Runtime.EntityDetails(self.Entity).get('component_data',{}).get('LOD',{})
            blocker=QSignalBlocker(forced);forced.SetOptions([(self.Tr('lod.automatic'),-1),(self.Tr('lod.culled'),-2)]+[(self.Tr('lod.level',index=i),i) for i in range(len(data.get('Levels',[])))]);forced.SetValue(data.get('Forced Level',-1));del blocker
        add=self._Button('lod.add_level',self._AddLevel);add.setEnabled(len(self._value)<8);self.Layout.addWidget(add)
