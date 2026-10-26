"""Ordered camera material passes, using the shared project asset picker."""
from copy import deepcopy
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QCheckBox,QPushButton,QLabel

ZERO='00000000-0000-0000-0000-000000000000'

class CameraMaterialsInput(QWidget):
    ValueChanged=Signal(object)
    def __init__(self,value,localization,picker,parent=None):
        super().__init__(parent);self.Tr=localization.Translate;self.Picker=picker
        self.Layout=QVBoxLayout(self);self.Layout.setContentsMargins(0,0,0,0);self.Layout.setSpacing(4)
        self.Rows=[];self.SetValue(value)
    def GetValue(self):return deepcopy(self.Rows)
    def SetValue(self,value):
        rows=[{'asset':str(row.get('asset',ZERO)),'enabled':bool(row.get('enabled',True))} for row in value]
        if rows==self.Rows and self.Layout.count():return
        self.Rows=rows
        self._Build()
    def _Build(self):
        while self.Layout.count():
            item=self.Layout.takeAt(0)
            if item.widget():item.widget().hide();item.widget().deleteLater()
        self.Layout.addWidget(QLabel(self.Tr("field.Post Process Materials"),self))
        for index,row in enumerate(self.Rows):
            container=QWidget(self);layout=QHBoxLayout(container);layout.setContentsMargins(0,0,0,0);layout.setSpacing(4)
            enabled=QCheckBox(container);enabled.setChecked(row['enabled']);enabled.setToolTip(self.Tr('camera.material_enabled'))
            enabled.toggled.connect(lambda value,i=index:self._Field(i,'enabled',value));layout.addWidget(enabled)
            layout.addWidget(self.Picker(row['asset'],lambda value,i=index:self._Field(i,'asset',value or ZERO)),1)
            for text,key,callback,allowed in (
                ('↑','camera.material_up',lambda checked=False,i=index:self._Move(i,-1),index>0),
                ('↓','camera.material_down',lambda checked=False,i=index:self._Move(i,1),index+1<len(self.Rows)),
                ('×','camera.material_remove',lambda checked=False,i=index:self._Remove(i),True)):
                button=QPushButton(text,container);button.setFixedWidth(22);button.setToolTip(self.Tr(key));button.setEnabled(allowed);button.clicked.connect(callback);layout.addWidget(button)
            self.Layout.addWidget(container)
        add=QPushButton(self.Tr('camera.material_add'),self);add.setEnabled(len(self.Rows)<32);add.clicked.connect(self._Add);self.Layout.addWidget(add)
    def _Field(self,index,key,value):self.Rows[index][key]=value;self.ValueChanged.emit(self.GetValue())
    def _Add(self):
        if len(self.Rows)>=32:return
        self.Rows.append({'asset':ZERO,'enabled':True});self._Build();self.ValueChanged.emit(self.GetValue())
    def _Remove(self,index):self.Rows.pop(index);self._Build();self.ValueChanged.emit(self.GetValue())
    def _Move(self,index,offset):
        target=index+offset
        if not 0<=target<len(self.Rows):return
        self.Rows[index],self.Rows[target]=self.Rows[target],self.Rows[index];self._Build();self.ValueChanged.emit(self.GetValue())
