"""Editor-wide extension management and content-only package builds."""
import json,sys
from pathlib import Path
import yaml
from PySide6.QtCore import Qt,QThread,Signal
from PySide6.QtWidgets import (QDialog,QVBoxLayout,QHBoxLayout,QTreeWidget,QTreeWidgetItem,QPushButton,QLabel,QLineEdit,QComboBox,QSplitter,QFormLayout,QPlainTextEdit,QWidget,QDialogButtonBox,QStyledItemDelegate)
from bazzalt.extensions import ReadManifest,Validate,ExtensionError
from Compiler.extensions import BuildExtensionPackage
from PySide6.QtWidgets import QFileDialog

def OpenFile(parent,title,folder,filter):
    dialog=QFileDialog(parent,title,folder,filter);dialog.setOption(QFileDialog.Option.DontUseNativeDialog,False);dialog.setFileMode(QFileDialog.FileMode.ExistingFile)
    return dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""

def SaveFile(parent,title,path,filter):
    dialog=QFileDialog(parent,title,str(Path(path).parent),filter);dialog.setOption(QFileDialog.Option.DontUseNativeDialog,False);dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave);dialog.setDefaultSuffix("bpack");dialog.selectFile(Path(path).name)
    return dialog.selectedFiles()[0] if dialog.exec() and dialog.selectedFiles() else ""

from .build_progress import BuildProgressDialog,BuildCompleteDialog

class ValueDelegate(QStyledItemDelegate):
    def createEditor(self,parent,option,index):
        if index.column()!=1:return None
        return super().createEditor(parent,option,index)

class PackageWorker(QThread):
    Progress=Signal(str);Finished=Signal(str,str)
    def __init__(self,options,parent):super().__init__(parent);self.Options=options
    def run(self):
        try:self.Finished.emit(str(BuildExtensionPackage(**self.Options,progress=self.Progress.emit,cancel=self.isInterruptionRequested)),'')
        except Exception as error:self.Finished.emit('',str(error))

class InstallWorker(QThread):
    Finished=Signal(str,str)
    def __init__(self,store,path,parent):super().__init__(parent);self.Store=store;self.Path=path
    def run(self):
        try:self.Finished.emit(self.Store.Import(self.Path),'')
        except Exception as error:self.Finished.emit('',str(error))

class ExtensionsDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Store=editor.Extensions.Store;self.Tr=editor.Localization.Translate
        self.setWindowTitle(self.Tr('extensions.title'));self.setWindowModality(Qt.WindowModality.WindowModal);self.resize(760,480)
        layout=QVBoxLayout(self);self.Tree=QTreeWidget();self.Tree.setHeaderLabels([self.Tr('extensions.name'),self.Tr('extensions.version'),self.Tr('extensions.status')]);layout.addWidget(self.Tree,1)
        self.Details=QLabel();self.Details.setTextFormat(Qt.TextFormat.PlainText);self.Details.setMaximumHeight(100);self.Details.setWordWrap(True);layout.addWidget(self.Details);self.Error=QLabel();self.Error.setTextFormat(Qt.TextFormat.PlainText);self.Error.setWordWrap(True);layout.addWidget(self.Error)
        row=QHBoxLayout();layout.addLayout(row)
        for key,callback in (('import',self._Import),('enable',lambda:self._Toggle(True)),('disable',lambda:self._Toggle(False)),('use_project',self._UseProject)):
            button=QPushButton(self.Tr('extensions.'+key));button.clicked.connect(callback);row.addWidget(button)
        row.addStretch();close=QPushButton(self.Tr('action.close'));close.clicked.connect(self.accept);row.addWidget(close)
        self.Tree.currentItemChanged.connect(self._Selected);self._Refresh()
    def _Refresh(self):
        self.Tree.clear()
        try:
            for item in self.Store.Installed():
                g=item['Manifest']['General'];row=QTreeWidgetItem([g['Name'],g['Version'],self.Tr('extensions.enabled' if item['Enabled'] else 'extensions.disabled')]);row.setData(0,Qt.ItemDataRole.UserRole,item['Id']);row.setToolTip(0,item['Id']);self.Tree.addTopLevelItem(row)
            self.Tree.resizeColumnToContents(0)
        except (OSError,ValueError) as error:self.Error.setText(str(error))
    def _Selected(self,item,*_):
        try:
            values={v['Id']:v for v in self.Store.Installed()};value=values.get(item.data(0,Qt.ItemDataRole.UserRole)) if item else None
            self.Details.setText(value['Manifest']['General']['ShortDescription'] if value else '')
        except (OSError,ValueError):self.Details.clear()
    def _Import(self):
        path=OpenFile(self,self.Tr('extensions.import'),'','BAZZALT Package (*.bpack)')
        if not path:return
        self.Progress=BuildProgressDialog(self.Editor);self.Progress.Stage.setText(self.Tr('extensions.importing'));self.Progress.Progress.setRange(0,0);self.Progress.Cancel.hide();self.Install=InstallWorker(self.Store,path,self);self.Install.Finished.connect(self._Imported);self.Install.start();self.Progress.exec()
    def _Imported(self,identity,error):
        if error:self.Progress.Fail(error)
        else:self.Progress.Finish();self.Error.clear();self._Refresh()
    def _UseProject(self):
        try:
            from Compiler.extensions import LockProject
            project=self.Editor.Runtime.ProjectInfo().get('project_path')
            if not project:raise ExtensionError(self.Tr('extensions.no_project'))
            if self.Editor.Runtime.IsPlaying():raise ExtensionError(self.Tr('extensions.stop_playing'))
            LockProject(project,self.Store.Resolve());self.Error.setText(self.Tr('extensions.project_updated'))
        except (ValueError,OSError) as error:self.Error.setText(str(error))
    def _Toggle(self,enabled):
        item=self.Tree.currentItem()
        if not item:return
        try:
            if self.Editor.Runtime.IsPlaying():raise ExtensionError(self.Tr('extensions.stop_playing'))
            self.Store.SetEnabled(item.data(0,Qt.ItemDataRole.UserRole),enabled);self.Error.setText(self.Tr('extensions.restart'));self._Refresh()
        except (OSError,ValueError) as error:self.Error.setText(str(error))

class PackageBuildDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate;self.Source=None;self.Manifest=None;self.Worker=None;self.InvalidFields=set()
        self.setWindowTitle(self.Tr('extensions.build'));self.setWindowModality(Qt.WindowModality.WindowModal);self.resize(800,580)
        root=QVBoxLayout(self);row=QHBoxLayout();root.addLayout(row);self.Path=QLineEdit();self.Path.setReadOnly(True);row.addWidget(self.Path,1);pick=QPushButton(self.Tr('extensions.manifest'));pick.clicked.connect(self._Pick);row.addWidget(pick)
        split=QSplitter();root.addWidget(split,1);self.Sections=QTreeWidget();self.Sections.setHeaderHidden(True);split.addWidget(self.Sections);self.Fields=QTreeWidget();self.Fields.setItemDelegate(ValueDelegate(self.Fields));self.Fields.setHeaderLabels([self.Tr('extensions.field'),self.Tr('extensions.value')]);split.addWidget(self.Fields);split.setSizes([170,600]);self.Sections.currentItemChanged.connect(self._Section);self.Fields.itemChanged.connect(self._Changed)
        form=QFormLayout();root.addLayout(form);self.Runtime=QLineEdit();self.Runtime.setPlaceholderText(self.Tr('extensions.runtime_path'));form.addRow(self.Tr('extensions.runtime'),self.Runtime)
        self.Compiler=QLineEdit();form.addRow(self.Tr('extensions.compiler'),self.Compiler);self.Target=QComboBox();self.Target.addItems(['windows-x64','windows-x86','linux-x64','linux-x86','macos-x64','macos-arm64']);self.Target.setCurrentText(__import__('bazzalt.platforms',fromlist=['HostPlatform']).HostPlatform()+'-'+__import__('bazzalt.platforms',fromlist=['HostArchitecture']).HostArchitecture());form.addRow(self.Tr('extensions.target'),self.Target)
        self.Error=QLabel();self.Error.setTextFormat(Qt.TextFormat.PlainText);self.Error.setWordWrap(True);root.addWidget(self.Error)
        row=QHBoxLayout();root.addLayout(row);advanced=QPushButton(self.Tr('extensions.advanced'));advanced.clicked.connect(self._Advanced);row.addWidget(advanced);row.addStretch();self.Build=QPushButton(self.Tr('extensions.build'));self.Build.clicked.connect(self._Build);row.addWidget(self.Build);close=QPushButton(self.Tr('action.close'));close.clicked.connect(self.reject);row.addWidget(close)
    def _Pick(self):
        source=OpenFile(self,self.Tr('extensions.manifest'),'','YAML (*.yaml *.yml)')
        if not source:return
        try:self.Manifest=ReadManifest(source);self.Source=Path(source);self.Path.setText(source);self._Refresh();self.Error.clear()
        except (OSError,ValueError) as error:self.Error.setText(str(error))
    def _Refresh(self):
        self.Sections.clear()
        for name in self.Manifest or {}:
            item=QTreeWidgetItem([name]);self.Sections.addTopLevelItem(item)
        if self.Sections.topLevelItemCount():self.Sections.setCurrentItem(self.Sections.topLevelItem(0))
    def _Section(self,item,*_):
        self.InvalidFields.clear();self.Fields.blockSignals(True);self.Fields.clear()
        def add(parent,key,value,path):
            row=QTreeWidgetItem([str(key),'' if isinstance(value,(dict,list)) else str(value).lower() if isinstance(value,bool) else str(value)]);row.setData(0,Qt.ItemDataRole.UserRole,path);row.setData(1,Qt.ItemDataRole.UserRole,type(value).__name__);parent.addChild(row) if parent else self.Fields.addTopLevelItem(row)
            if isinstance(value,(dict,list)):
                for k,v in (value.items() if isinstance(value,dict) else enumerate(value)):add(row,k,v,[*path,k])
            else:row.setFlags(row.flags()|Qt.ItemFlag.ItemIsEditable)
            row.setExpanded(True)
        if item:
            name=item.text(0);value=self.Manifest[name];add(None,name,value,[name])
        self.Fields.resizeColumnToContents(0);self.Fields.blockSignals(False)
    def _Changed(self,item,column):
        if column!=1:return
        try:
            text=item.text(1);kind=item.data(1,Qt.ItemDataRole.UserRole)
            if kind=='bool':
                if text.lower() not in ('true','false'):raise ValueError(self.Tr('extensions.bool_value'))
                value=text.lower()=='true'
            elif kind=='int':value=int(text)
            elif kind=='float':
                import math
                value=float(text)
                if not math.isfinite(value):raise ValueError(self.Tr('extensions.finite_value'))
            elif kind=='str':value=text
            else:value=json.loads(text)
            path=item.data(0,Qt.ItemDataRole.UserRole);target=self.Manifest
            for key in path[:-1]:target=target[key]
            target[path[-1]]=value;self.InvalidFields.discard(id(item));self.Error.clear()
        except (ValueError,TypeError) as error:self.InvalidFields.add(id(item));self.Error.setText(str(error))
    def _Advanced(self):
        if self.Manifest is None:return
        dialog=QDialog(self);dialog.setWindowTitle(self.Tr('extensions.advanced'));dialog.resize(700,500);layout=QVBoxLayout(dialog);text=QPlainTextEdit(yaml.safe_dump(self.Manifest,sort_keys=False,allow_unicode=True));layout.addWidget(text);buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel);buttons.accepted.connect(dialog.accept);buttons.rejected.connect(dialog.reject);layout.addWidget(buttons)
        if dialog.exec():
            try:
                from bazzalt.extensions import UniqueLoader
                self.Manifest=Validate(yaml.load(text.toPlainText(),Loader=UniqueLoader),self.Source.parent);self._Refresh();self.Error.clear()
            except (ValueError,yaml.YAMLError) as error:self.Error.setText(str(error))
    def _Build(self):
        if not self.Source or self.Worker and self.Worker.isRunning():return
        try:
            if self.InvalidFields:raise ExtensionError(self.Tr("extensions.invalid_fields"))
            self.Manifest=Validate(self.Manifest,self.Source.parent)
        except (ValueError,OSError) as error:self.Error.setText(str(error));return
        output=SaveFile(self,self.Tr('extensions.build'),str(self.Source.with_suffix('.bpack')),'BAZZALT Package (*.bpack)')
        if not output:return
        # The validated visual edits are an explicit authoring save.
        self.Source.write_text(yaml.safe_dump(self.Manifest,sort_keys=False,allow_unicode=True),encoding='utf-8')
        platform,architecture=self.Target.currentText().split('-');self.Progress=BuildProgressDialog(self.Editor);self.Progress.Stage.setText(self.Tr('extensions.compiling'));self.Progress.Progress.setRange(0,0)
        options=dict(source=self.Source,destination=output,runtime=self.Runtime.text(),platform=platform,architecture=architecture,compiler_path=self.Compiler.text())
        self.Worker=PackageWorker(options,self);self.Worker.Progress.connect(lambda value:self.Progress.Stage.setText(self.Tr('extensions.compiling_item',name=value)));self.Worker.Finished.connect(self._Finished);self.Progress.CancelRequested.connect(self.Worker.requestInterruption);self.Worker.start();self.Progress.exec()
    def _Finished(self,output,error):
        if error:self.Progress.Fail(error)
        else:self.Progress.Finish();BuildCompleteDialog(self.Editor,Path(output)).exec()
    def reject(self):
        if not self.Worker or not self.Worker.isRunning():super().reject()
