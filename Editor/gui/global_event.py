"""Themed Global Event schema inspector."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QLabel,QPushButton
from .widgets import StringInput,EnumInput
from ..global_events import Read,Write,TYPES
class GlobalEventEditor(QWidget):
    def __init__(self,controller,path):
        super().__init__();self.Controller=controller;self.Path=path;self.Tr=controller.Window.Localization.Translate;self.Rows=[]
        root=QVBoxLayout(self);root.setContentsMargins(0,0,0,0);self.Name=StringInput();root.addWidget(QLabel(self.Tr('events.name')));root.addWidget(self.Name)
        root.addWidget(QLabel(self.Tr('events.arguments')));self.Fields=QVBoxLayout();root.addLayout(self.Fields)
        self.Error=QLabel();self.Error.setTextFormat(Qt.TextFormat.PlainText);self.Error.setWordWrap(True);root.addWidget(self.Error)
        buttons=QHBoxLayout();add=QPushButton(self.Tr('events.add_argument'));add.clicked.connect(lambda:self.AddArgument());buttons.addWidget(add);apply=QPushButton(self.Tr('events.apply'));apply.clicked.connect(self.Apply);buttons.addWidget(apply);root.addLayout(buttons)
        try:
            data=Read(path);self.Name.SetValue(data['Name'])
            for row in data['Arguments']:self.AddArgument(row)
        except (OSError,ValueError) as error:self.Error.setText(str(error))
        self.setEnabled(not controller.Runtime.IsPlaying())
    def AddArgument(self,value=None):
        if len(self.Rows)>=32:return
        value=value or dict(Name='Argument'+str(len(self.Rows)+1),Type='Number');widget=QWidget();row=QHBoxLayout(widget);row.setContentsMargins(0,0,0,0)
        name=StringInput();name.SetValue(value['Name']);kind=EnumInput();kind.SetOptions([(self.Tr('events.type.'+t),t) for t in TYPES]);kind.SetValue(value['Type'])
        remove=QPushButton(self.Tr('events.remove_argument'));row.addWidget(name,1);row.addWidget(kind,1);row.addWidget(remove);entry=(widget,name,kind);self.Rows.append(entry);self.Fields.addWidget(widget);remove.clicked.connect(lambda:self.Remove(entry))
    def Remove(self,entry):
        if entry in self.Rows:self.Rows.remove(entry);entry[0].hide();entry[0].deleteLater()
    def Apply(self):
        try:
            if self.Controller.Runtime.IsPlaying():return
            value=dict(Version=1,Name=self.Name.GetValue(),Arguments=[dict(Name=n.GetValue(),Type=t.GetValue()) for _,n,t in self.Rows]);Write(self.Path,value)
            if not self.Controller.Runtime.RefreshAsset(self.Path):raise ValueError(self.Controller.Runtime.LastError())
            self.Error.clear();self.Controller.Window.AssetBrowser.UpdateAssetIssues()
        except (OSError,ValueError) as error:self.Error.setText(str(error))
