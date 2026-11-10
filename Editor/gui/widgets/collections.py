"""Typed inspector collections with theme-owned cards and explicit item actions."""
from copy import deepcopy
from PySide6.QtCore import Signal, Qt
from PySide6.QtWidgets import QWidget, QFrame, QVBoxLayout, QHBoxLayout, QToolButton, QLabel, QTabWidget, QPushButton, QMenu, QSizePolicy
from .fields import FieldWidget
from .options_button import OptionsButton


class ListInput(FieldWidget):
    ValueChanged = Signal(object)

    def __init__(self, values=(), *, factory, default=None, maximum=256, minimum=0,
                 translate=lambda key:key, title=None, item_name=None, add_text=None, parent=None):
        super().__init__(parent)
        self.setObjectName('InspectorCollection')
        self.Factory=factory;self.Default=default;self.Maximum=maximum;self.Minimum=minimum;self.Tr=translate
        self.Title=title;self.ItemName=item_name or self.Tr('collection.item');self.AddText=add_text or self.Tr('collection.add')
        self.Layout=QVBoxLayout(self);self.Layout.setContentsMargins(0,0,0,0);self.Layout.setSpacing(6)
        self.SetValue(values)

    def GetValue(self):return deepcopy(self._values)

    def SetValue(self, values):
        if not isinstance(values,(list,tuple)) or not self.Minimum<=len(values)<=self.Maximum:
            raise ValueError('Invalid list field size')
        self._values=deepcopy(list(values));self._Rebuild()

    def _Label(self, value, index):
        name=value.get('Name','') if isinstance(value,dict) else ''
        return str(name).strip() or f'{self.ItemName} {index+1}'

    def _Changed(self, index, value):
        copied=deepcopy(value)
        if isinstance(self._values[index],dict) and isinstance(copied,dict):
            self._values[index].clear();self._values[index].update(copied)
        else:self._values[index]=copied
        self._headers[index].setText(self._Label(copied,index))
        self._headers[index].setToolTip(self._Label(copied,index))
        self.ValueChanged.emit(self.GetValue())

    def Reindex(self, key='_Index'):
        for index,value in enumerate(self._values):
            if isinstance(value,dict):value[key]=index

    def _Expanded(self):return [button.isChecked() for button in getattr(self,'_headers',[])]

    def _ExpandAll(self, expanded):
        for button in self._headers:button.setChecked(expanded)

    def _Options(self, button):
        menu=QMenu(self)
        for key,expanded in (('properties.expand_all',True),('properties.collapse_all',False)):
            action=menu.addAction(self.Tr(key));action.setEnabled(bool(self._headers))
            action.triggered.connect(lambda _,state=expanded:self._ExpandAll(state))
        menu.exec(button.mapToGlobal(button.rect().bottomLeft()))
        menu.deleteLater()

    def _Rebuild(self, expanded=None):
        expanded=self._Expanded() if expanded is None else expanded
        self._headers=[]
        while self.Layout.count():
            item=self.Layout.takeAt(0)
            if item.widget():item.widget().hide();item.widget().deleteLater()
        toolbar=QWidget(self);toolbar.setObjectName('CollectionToolbar')
        row=QHBoxLayout(toolbar);row.setContentsMargins(2,0,0,0);row.setSpacing(6)
        label=QLabel(self.Title or self.ItemName,toolbar);label.setObjectName('CollectionTitle');label.setWordWrap(True);row.addWidget(label,1)
        count=QLabel(str(len(self._values)),toolbar);count.setObjectName('CollectionCount');row.addWidget(count)
        options=OptionsButton(toolbar);options.setToolTip(self.Tr('hierarchy.helpers'));options.clicked.connect(lambda:self._Options(options));row.addWidget(options)
        self.Layout.addWidget(toolbar)
        if not self._values:
            empty=QLabel(self.Tr('collection.empty'),self);empty.setObjectName('CollectionEmpty');empty.setWordWrap(True);self.Layout.addWidget(empty)
        for index,value in enumerate(self._values):
            container=QFrame(self);container.setObjectName('CollectionCard')
            column=QVBoxLayout(container);column.setContentsMargins(0,0,0,0);column.setSpacing(0)
            head=QWidget(container);head.setObjectName('CollectionHeader')
            row=QHBoxLayout(head);row.setContentsMargins(4,2,4,2);row.setSpacing(2)
            toggle=QToolButton(head);toggle.setObjectName('CollectionToggle');toggle.setCheckable(True)
            toggle.setText(self._Label(value,index));toggle.setArrowType(Qt.ArrowType.RightArrow)
            toggle.setToolTip(self._Label(value,index));toggle.setMinimumWidth(0)
            toggle.setSizePolicy(QSizePolicy.Policy.Ignored,QSizePolicy.Policy.Fixed)
            toggle.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon);row.addWidget(toggle,1)
            for arrow,offset,key in ((Qt.ArrowType.UpArrow,-1,'collection.move_up'),(Qt.ArrowType.DownArrow,1,'collection.move_down')):
                button=QToolButton(head);button.setObjectName('CollectionAction');button.setArrowType(arrow)
                button.setToolTip(self.Tr(key));button.setEnabled(0<=index+offset<len(self._values))
                button.clicked.connect(lambda _,i=index,o=offset:self.Move(i,o));row.addWidget(button)
            remove=QToolButton(head);remove.setObjectName('CollectionAction');remove.setText('−')
            remove.setToolTip(self.Tr('collection.remove'));remove.setEnabled(len(self._values)>self.Minimum)
            remove.clicked.connect(lambda _,i=index:self.Remove(i));row.addWidget(remove);column.addWidget(head)
            body=self.Factory(value,lambda next_value,i=index:self._Changed(i,next_value))
            body.setParent(container);body.setMinimumWidth(0);body.hide();column.addWidget(body)
            toggle.toggled.connect(lambda checked,b=body,t=toggle:(b.setVisible(checked),t.setArrowType(Qt.ArrowType.DownArrow if checked else Qt.ArrowType.RightArrow)))
            self._headers.append(toggle)
            if index<len(expanded):toggle.setChecked(expanded[index])
            self.Layout.addWidget(container)
        self.AddButton=QPushButton(self.AddText,self);self.AddButton.setObjectName('CollectionAdd')
        self.AddButton.setEnabled(len(self._values)<self.Maximum);self.AddButton.clicked.connect(self.Add);self.Layout.addWidget(self.AddButton)

    def Add(self):
        if len(self._values)>=self.Maximum:return
        expanded=self._Expanded()+[True]
        self._values.append(deepcopy(self.Default() if callable(self.Default) else self.Default))
        self._Rebuild(expanded);self.ValueChanged.emit(self.GetValue())

    def Remove(self, index):
        if len(self._values)<=self.Minimum:return
        expanded=self._Expanded();del expanded[index];del self._values[index]
        self._Rebuild(expanded);self.ValueChanged.emit(self.GetValue())

    def Move(self, index, offset):
        target=index+offset
        if not 0<=target<len(self._values):return
        expanded=self._Expanded();expanded[index],expanded[target]=expanded[target],expanded[index]
        self._values[index],self._values[target]=self._values[target],self._values[index]
        self._Rebuild(expanded);self.ValueChanged.emit(self.GetValue())


class TabInput(QTabWidget):
    """Shared field container. Labels are localized by the owning inspector."""
    def AddSection(self,title,widget):self.addTab(widget,title);return widget
