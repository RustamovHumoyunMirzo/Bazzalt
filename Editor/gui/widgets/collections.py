"""Bounded, reusable list and tab fields; values stay independent of Qt widgets."""
from copy import deepcopy
from PySide6.QtCore import Signal,Qt
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QToolButton,QLabel,QTabWidget
from .fields import FieldWidget

class ListInput(FieldWidget):
    ValueChanged=Signal(object)
    def __init__(self,values=(),*,factory,default=None,maximum=256,minimum=0,translate=lambda key:key,parent=None):
        super().__init__(parent);self.Factory=factory;self.Default=default;self.Maximum=maximum;self.Minimum=minimum;self.Tr=translate
        self.Layout=QVBoxLayout(self);self.Layout.setContentsMargins(0,0,0,0);self.Layout.setSpacing(4);self.SetValue(values)
    def GetValue(self):return deepcopy(self._values)
    def SetValue(self,values):
        if not isinstance(values,(list,tuple)) or not self.Minimum<=len(values)<=self.Maximum:raise ValueError('Invalid list field size')
        self._values=deepcopy(list(values));self._Rebuild()
    def _Changed(self,index,value):
        copied=deepcopy(value)
        if isinstance(self._values[index],dict) and isinstance(copied,dict):self._values[index].clear();self._values[index].update(copied)
        else:self._values[index]=copied
        if isinstance(copied,dict) and index<len(self._headers):self._headers[index].setText(str(copied.get('Name',index+1)))
        self.ValueChanged.emit(self.GetValue())
    def Reindex(self,key='_Index'):
        for index,value in enumerate(self._values):
            if isinstance(value,dict):value[key]=index

    def _Rebuild(self):
        expanded=[button.isChecked() for button in getattr(self,'_headers',[])];self._headers=[]
        while self.Layout.count():
            item=self.Layout.takeAt(0)
            if item.widget():item.widget().hide();item.widget().deleteLater()
        for index,value in enumerate(self._values):
            container=QWidget(self);column=QVBoxLayout(container);column.setContentsMargins(0,0,0,0);column.setSpacing(3)
            head=QWidget(container);row=QHBoxLayout(head);row.setContentsMargins(0,0,0,0);row.setSpacing(3)
            toggle=QToolButton();toggle.setCheckable(True);toggle.setText(str(value.get('Name',index+1)) if isinstance(value,dict) else str(index+1));toggle.setArrowType(Qt.ArrowType.RightArrow);toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon);row.addWidget(toggle,1)
            for text,offset in (('↑',-1),('↓',1)):
                button=QToolButton();button.setText(text);button.setEnabled(0<=index+offset<len(self._values));button.clicked.connect(lambda _,i=index,o=offset:self.Move(i,o));row.addWidget(button)
            remove=QToolButton();remove.setText('−');remove.setToolTip(self.Tr('terrain.remove'));remove.setEnabled(len(self._values)>self.Minimum);remove.clicked.connect(lambda _,i=index:self.Remove(i));row.addWidget(remove);column.addWidget(head)
            body=self.Factory(value,lambda next_value,i=index:self._Changed(i,next_value));body.setParent(container);body.hide();column.addWidget(body)
            toggle.toggled.connect(lambda checked,b=body,t=toggle:(b.setVisible(checked),t.setArrowType(Qt.ArrowType.DownArrow if checked else Qt.ArrowType.RightArrow)))
            self._headers.append(toggle)
            if index<len(expanded):toggle.setChecked(expanded[index])
            self.Layout.addWidget(container)
        add=QToolButton();add.setText(self.Tr('terrain.add'));add.setEnabled(len(self._values)<self.Maximum);add.clicked.connect(self.Add);self.Layout.addWidget(add)
    def Add(self):
        if len(self._values)>=self.Maximum:return
        self._values.append(deepcopy(self.Default() if callable(self.Default) else self.Default));self._Rebuild();self.ValueChanged.emit(self.GetValue())
    def Remove(self,index):
        if len(self._values)<=self.Minimum:return
        del self._values[index];self._Rebuild();self.ValueChanged.emit(self.GetValue())
    def Move(self,index,offset):
        target=index+offset
        if not 0<=target<len(self._values):return
        self._values[index],self._values[target]=self._values[target],self._values[index];self._Rebuild();self.ValueChanged.emit(self.GetValue())

class TabInput(QTabWidget):
    """Shared field container. Labels are localized by the owning inspector."""
    def AddSection(self,title,widget):self.addTab(widget,title);return widget
