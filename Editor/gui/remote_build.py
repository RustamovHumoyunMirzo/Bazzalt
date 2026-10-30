"""Modal SSH worker profiles, asynchronous checks and content builds."""
import copy
from pathlib import Path
import threading
import uuid
from PySide6.QtCore import QObject,Signal,Qt
from PySide6.QtWidgets import QDialog,QVBoxLayout,QHBoxLayout,QFormLayout,QWidget,QScrollArea,QLabel,QLineEdit,QComboBox,QSpinBox,QCheckBox,QPushButton,QFileDialog
from .. import remote_build as Remote

class Task(QObject):
    Done=Signal(object);Failed=Signal(str);Event=Signal(object)
    def Start(self,function):
        def run():
            try:
                result=function();self.Done.emit(result)
            except Exception as error:
                try:self.Failed.emit(str(error))
                except RuntimeError:pass
        threading.Thread(target=run,daemon=True).start()

class RemoteBuildDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate;self.Profiles=[]
        stored=Remote.Store().Load().get('profiles',[]);stored=stored if isinstance(stored,list) else []
        for profile in stored[:64]:
            try:self.Profiles.append(Remote.Validate(profile))
            except (ValueError,TypeError):pass
        self.Fields={};self.Busy=False;self.Cancelled=threading.Event();self.Current=-1
        self.setWindowTitle(self.Tr('remote.title'));self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(740,700)
        root=QVBoxLayout(self);row=QHBoxLayout();self.ProfilesBox=QComboBox();row.addWidget(self.ProfilesBox,1)
        for key,handler in [('new',self.New),('delete',self.Delete)]:
            button=QPushButton(self.Tr('remote.'+key));button.clicked.connect(handler);row.addWidget(button)
        root.addLayout(row);page=QWidget();self.FormPage=page;form=QFormLayout(page);scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);root.addWidget(scroll,1)
        choices={'authentication':('key','password'),'worker_platform':('windows','linux','macos'),'post_build':('download','leave_remote','open_remote')}
        for key,default in Remote.DEFAULT.items():
            if key=='id':continue
            if key in choices:
                field=QComboBox()
                for option in choices[key]:field.addItem(self.Tr('remote.option.'+option),option)
            elif isinstance(default,bool):field=QCheckBox()
            elif key=='port':field=QSpinBox();field.setRange(1,65535)
            else:field=QLineEdit()
            self.Fields[key]=field
            if key in ('private_key','known_hosts'):
                container=QWidget();layout=QHBoxLayout(container);layout.setContentsMargins(0,0,0,0);layout.addWidget(field,1);browse=QPushButton(self.Tr('remote.browse'));browse.clicked.connect(lambda _,f=field:self.Pick(f));layout.addWidget(browse);form.addRow(self.Tr('remote.field.'+key),container)
            else:form.addRow(self.Tr('remote.field.'+key),field)
        self.Credential=QLineEdit();self.Credential.setEchoMode(QLineEdit.EchoMode.Password);self.CredentialLabel=QLabel();position=form.getWidgetPosition(self.Fields['private_key'].parentWidget())[0]+1;form.insertRow(position,self.CredentialLabel,self.Credential)
        saved=form.takeRow(self.Fields['save_secret']);form.insertRow(position+1,saved.labelItem.widget(),self.Fields['save_secret'])
        def auth(*_):
            key=self.Fields['authentication'].currentData()=='key';container=self.Fields['private_key'].parentWidget();container.setVisible(key);form.labelForField(container).setVisible(key);self.CredentialLabel.setText(self.Tr('remote.passphrase' if key else 'remote.password'))
        self.Fields['authentication'].currentIndexChanged.connect(auth);auth()
        self.Status=QLabel(self.Tr('remote.disconnected'));self._tone='text_muted';self.Status.setTextFormat(Qt.TextFormat.PlainText);self.Status.setWordWrap(True);root.addWidget(self.Status)
        if getattr(editor,'ThemeManager',None):editor.ThemeManager.ThemeChanged.connect(self._Color)
        row=QHBoxLayout();self.SaveButton=QPushButton(self.Tr('remote.save'));self.TestButton=QPushButton(self.Tr('remote.test'));self.BuildButton=QPushButton(self.Tr('remote.build'));self.CloseButton=QPushButton(self.Tr('remote.close'))
        for button in (self.SaveButton,self.TestButton,self.BuildButton,self.CloseButton):row.addWidget(button)
        root.addLayout(row);self.SaveButton.clicked.connect(self.Save);self.TestButton.clicked.connect(lambda:self.Run(False));self.BuildButton.clicked.connect(lambda:self.Run(True));self.CloseButton.clicked.connect(self.reject);self.ProfilesBox.currentIndexChanged.connect(self.Select)
        for profile in self.Profiles:self.ProfilesBox.addItem(profile['name'],profile['id'])
        if not self.Profiles:self.New()
        else:self.Select(0)
    def _Color(self,*_):
        if getattr(self.Editor,'ThemeManager',None):self.Status.setStyleSheet('color:'+getattr(self.Editor.ThemeManager.GetTheme(),self._tone))
    def Pick(self,field):
        path,_=QFileDialog.getOpenFileName(self,self.Tr('remote.browse'),str(Path.home()/'.ssh'))
        if path:field.setText(path)
    def New(self):
        if self.Busy:return
        value=copy.deepcopy(Remote.DEFAULT);value.update(id=uuid.uuid4().hex,name=self.Tr('remote.new_name'));self.Profiles.append(value);self.ProfilesBox.addItem(value['name'],value['id']);self.ProfilesBox.setCurrentIndex(len(self.Profiles)-1);self.Select(len(self.Profiles)-1)
    def Delete(self):
        if self.Busy or self.Current<0:return
        profile=self.Profiles[self.Current]
        try:
            if profile['save_secret']:Remote.Secret(profile,'')
            del self.Profiles[self.Current];self.ProfilesBox.removeItem(self.Current);Remote.Store().Save(dict(profiles=self.Profiles))
            if not self.Profiles:self.New()
        except Exception as error:self.Status.setText(str(error))
    def Select(self,index):
        if self.Busy or not 0<=index<len(self.Profiles):return
        self.Current=index;value=Remote.DEFAULT|self.Profiles[index]
        for key,field in self.Fields.items():
            if isinstance(field,QComboBox):field.setCurrentIndex(field.findData(value[key]))
            elif isinstance(field,QCheckBox):field.setChecked(value[key])
            elif isinstance(field,QSpinBox):field.setValue(value[key])
            else:field.setText(value[key])
        self.Credential.clear();self._tone='text_muted';self._Color();self.Status.setText(self.Tr('remote.disconnected'))
    def Value(self):
        value=copy.deepcopy(self.Profiles[self.Current])
        for key,field in self.Fields.items():value[key]=field.currentData() if isinstance(field,QComboBox) else field.isChecked() if isinstance(field,QCheckBox) else field.value() if isinstance(field,QSpinBox) else field.text().strip()
        return value
    def Save(self):
        try:
            value=Remote.Validate(self.Value())
            if value['save_secret']:Remote.Secret(value,self.Credential.text() or Remote.Secret(value))
            elif self.Profiles[self.Current].get('save_secret'):Remote.Secret(value,'')
            self.Profiles[self.Current]=value;self.ProfilesBox.setItemText(self.Current,value['name']);Remote.Store().Save(dict(profiles=self.Profiles));self.Status.setText(self.Tr('remote.saved'));return True
        except Exception as error:self.Status.setText(str(error));return False
    def Run(self,build):
        if self.Busy or not self.Save():return
        value=self.Value();secret=self.Credential.text()
        try:
            if not secret and value['save_secret']:secret=Remote.Secret(value)
            if build and (self.Editor.Runtime.IsPlaying() or self.Editor.Controller.IsDirty):raise ValueError(self.Tr('remote.save_stop_first'))
            project=Path(self.Editor.Runtime.ProjectInfo()['project_path']) if build else None
        except Exception as error:self.Status.setText(str(error));return
        self.Busy=True;self.FormPage.setEnabled(False);self.Cancelled.clear();self.Status.setText(self.Tr('remote.working'));self.CloseButton.setText(self.Tr('remote.cancel'))
        for button in (self.TestButton,self.BuildButton,self.SaveButton,self.ProfilesBox):button.setEnabled(False)
        self.Task=Task(self);self.Task.Done.connect(self.Done);self.Task.Failed.connect(self.Failed);self.Task.Event.connect(lambda event:self.Status.setText(self.Tr('remote.stage',stage=self.Tr('build.stage.'+event.get('stage','')))))
        self.Task.Start(lambda:Remote.Build(project,value,secret,cancel=self.Cancelled,on_event=self.Task.Event.emit) if build else Remote.Test(value,secret,self.Cancelled))
    def Finish(self):
        self.Busy=False;self.FormPage.setEnabled(True)
        for button in (self.TestButton,self.BuildButton,self.SaveButton,self.ProfilesBox):button.setEnabled(True)
        self.CloseButton.setText(self.Tr('remote.close'))
    def Done(self,value):
        self.Finish();self._tone='success';self._Color()
        result=value.get('download') or value.get('output')
        if not result:result=value['platform']+' / '+value['architecture']+'; '+(value.get('cpp_compiler_version') or self.Tr('remote.cpp_unavailable'))
        self.Status.setText(self.Tr('remote.complete',result=result))
    def Failed(self,error):self.Finish();self._tone='error';self._Color();self.Status.setText(self.Tr('remote.failed',error=error))
    def reject(self):
        if self.Busy:self.Cancelled.set();self.Status.setText(self.Tr('remote.cancelling'))
        else:super().reject()
