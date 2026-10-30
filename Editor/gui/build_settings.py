"""Shared build options plus independent, collapsible platform profiles."""
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog,QVBoxLayout,QHBoxLayout,QListWidget,QStackedWidget,QWidget,QFormLayout,QScrollArea,QLineEdit,QCheckBox,QComboBox,QSpinBox,QPushButton,QLabel,QPlainTextEdit,QToolButton,QFileDialog
from Compiler.settings import Load,Save,ApplyPreset,Effective,PathFor
from bazzalt.platforms import HostPlatform,HostArchitecture

class BuildSettingsDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate;self.Project=Path(editor.Runtime.ProjectInfo()['project_path']);self.Values=Load(self.Project);self.Controls={};self.PlatformControls={};self.PlatformGroups={};self._applying_preset=False
        if not PathFor(self.Project).exists() and HostPlatform() in self.Values['platforms']:
            self.Values['target']['platform']=HostPlatform()
            self.Values['platforms'][HostPlatform()]['architecture']=HostArchitecture()
        self.setWindowTitle(self.Tr('build.settings'));self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(820,600);self.setMinimumSize(620,440)
        root=QVBoxLayout(self);body=QHBoxLayout();self.Sections=QListWidget();self.Sections.setFixedWidth(160);self.Pages=QStackedWidget();body.addWidget(self.Sections);body.addWidget(self.Pages,1);root.addLayout(body,1)
        sections=(('production',tuple(self.Values['production'])),('target',('platform','startup_scene')),('window',tuple(k for k in self.Values['window'] if k not in ('backend','display_server'))),('intro',tuple(self.Values['intro'])),('packaging',('archive','include_assets','exclude_assets')),('security',tuple(k for k in self.Values['security'] if k!='key_storage')+('format','chunk_mb','decoy_kb')))
        for section,keys in sections:
            self.Sections.addItem(self.Tr('build.section.'+section));page=QWidget();form=QFormLayout(page);form.setContentsMargins(14,12,14,12);form.setSpacing(10);scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);self.Pages.addWidget(scroll)
            for key in keys:
                storage='packaging' if key in ('format','chunk_mb','decoy_kb') else section
                field=self._Field(key,self.Values[storage][key]);self.Controls[(storage,key)]=field;form.addRow(self.Tr('build.field.'+key),field)
            if section=='target':
                for platform,values in self.Values['platforms'].items():
                    container=QWidget();layout=QVBoxLayout(container);layout.setContentsMargins(0,6,0,6);header=QToolButton();header.setText(self.Tr('build.option.'+platform));header.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon);header.setCheckable(True);header.setChecked(platform==self.Values['target']['platform']);layout.addWidget(header)
                    content=QWidget();fields=QFormLayout(content);fields.setContentsMargins(16,6,0,6);layout.addWidget(content);content.setVisible(header.isChecked());self.PlatformGroups[platform]=(header,content)
                    def expanded(state,h=header,c=content):h.setArrowType(Qt.ArrowType.DownArrow if state else Qt.ArrowType.RightArrow);c.setVisible(state)
                    header.toggled.connect(expanded);expanded(header.isChecked())
                    for key,value in values.items():
                        if key.startswith('wsl_') and platform!='linux':continue
                        options=(('x64','arm64') if platform=='macos' else ('x64','x86','arm64')) if key=='architecture' else (('automatic','metal') if platform=='macos' else ('automatic','vulkan','opengl')) if key=='backend' else ('embedded','file','environment',*(['dpapi_user'] if platform=='windows' else ['keychain'] if platform=='macos' else [])) if key=='key_storage' else None
                        if key=='display_server':options=('automatic','x11','wayland')
                        if key=='build_method':
                            from Compiler.cross import METHODS
                            options=('automatic','native','content_only',*METHODS.get((HostPlatform(),platform),()))
                        field=self._Field(key,value,options);self.PlatformControls[(platform,key)]=field
                        if key in ('runtime_directory','toolchain','sysroot'):
                            fieldContainer=QWidget();row=QHBoxLayout(fieldContainer);row.setContentsMargins(0,0,0,0);row.addWidget(field,1);browse=QPushButton(self.Tr('remote.browse'));row.addWidget(browse)
                            browse.clicked.connect(lambda _,f=field,k=key:self._BrowsePath(f,k));fields.addRow(self.Tr('build.field.'+key),fieldContainer)
                        else:fields.addRow(self.Tr('build.field.'+key),field)
                    detect=QPushButton(self.Tr('build.detect_toolchain'));fields.addRow(detect);detect.clicked.connect(lambda _,p=platform:self._Detect(p))
                    form.addRow(container)
        self.Controls[('security','preset')].activated.connect(self._Preset)
        for pair in (('security','encryption'),('security','encrypt_manifests'),('packaging','format'),('packaging','chunk_mb'),('packaging','decoy_kb')):
            field=self.Controls[pair];(field.currentIndexChanged if isinstance(field,QComboBox) else field.toggled if isinstance(field,QCheckBox) else field.valueChanged).connect(self._Custom)
        for platform in self.Values['platforms']:self.PlatformControls[(platform,'key_storage')].currentIndexChanged.connect(self._Custom)
        self.Sections.currentRowChanged.connect(self.Pages.setCurrentIndex);self.Sections.setCurrentRow(0)
        self.TargetWarning=QLabel();self.TargetWarning.setWordWrap(True);root.addWidget(self.TargetWarning);self.Error=QLabel();self.Error.setWordWrap(True);root.addWidget(self.Error)
        self.Controls[('target','platform')].currentIndexChanged.connect(self._PlatformChanged)
        for (platform,key),field in self.PlatformControls.items():
            if key in ('architecture','key_storage','build_method'):field.currentIndexChanged.connect(self._TargetWarning)
        self._PlatformChanged()
        buttons=QHBoxLayout();buttons.addStretch();cancel=QPushButton(self.Tr('preferences.cancel'));apply=QPushButton(self.Tr('preferences.apply'));buttons.addWidget(cancel);buttons.addWidget(apply);root.addLayout(buttons);cancel.clicked.connect(self.reject);apply.clicked.connect(self._Apply)
    def _Field(self,key,value,options=None):
        choices={'platform':('windows','linux','macos'),'mode':('windowed','maximized','borderless','fullscreen'),'format':('pak','loose'),'preset':('low','medium','high','very_high','custom'),'encryption':('none','aes-256-gcm')};options=options or choices.get(key)
        if options:
            field=QComboBox()
            for option in options:field.addItem(self.Tr('build.option.'+option),option)
            field.setCurrentIndex(field.findData(value));return field
        if isinstance(value,bool):field=QCheckBox();field.setChecked(value);return field
        if isinstance(value,int):
            field=QSpinBox();low,high=(320,16384) if key=='width' else (200,16384) if key=='height' else (4,256) if key=='chunk_mb' else (0,30000) if key=='duration_ms' else (48,512) if key=='logo_size' else (0,1024);field.setRange(low,high);field.setValue(value);return field
        if key in ('license','description','include_assets','exclude_assets'):field=QPlainTextEdit(str(value));field.setMaximumHeight(90);return field
        return QLineEdit(str(value))
    def _BrowsePath(self,field,key):
        value=QFileDialog.getOpenFileName(self,self.Tr('remote.browse'),field.text())[0] if key=='toolchain' else QFileDialog.getExistingDirectory(self,self.Tr('remote.browse'),field.text())
        if value:field.setText(value)
    def _Detect(self,platform):
        from Compiler.cross import Detect,METHODS
        method=self.PlatformControls[(platform,'build_method')].currentData();arch=self.PlatformControls[(platform,'architecture')].currentData();choices=(('native',) if platform==HostPlatform() else METHODS.get((HostPlatform(),platform),())) if method=='automatic' else (method,)
        for option in choices:
            found=Detect(option,platform,arch)
            if found:
                self.PlatformControls[(platform,'build_method')].setCurrentIndex(self.PlatformControls[(platform,'build_method')].findData(option));self.PlatformControls[(platform,'toolchain')].setText(str(found));return
        self.Error.setText(self.Tr('build.toolchain_not_found'))
    def _PlatformChanged(self,*_):
        selected=self.Controls[('target','platform')].currentData()
        for platform,(header,_) in self.PlatformGroups.items():
            font=header.font();font.setBold(platform==selected);header.setFont(font)
        self.PlatformGroups[selected][0].setChecked(True)
        for section,key in (('target','architecture'),('target','runtime_directory'),('target','build_method'),('window','backend'),('security','key_storage')):self.Controls[(section,key)]=self.PlatformControls[(selected,key)]
        self._TargetWarning()
    def _TargetWarning(self,*_):
        from bazzalt.platforms import TargetError
        value=Effective(self.GetValues());error=TargetError({**value['target'],'key_storage':value['security']['key_storage']});self.TargetWarning.setText(self.Tr('build.unsupported_target',error=error) if error else '');self.TargetWarning.setVisible(bool(error))
    @staticmethod
    def _Value(field):return field.currentData() if isinstance(field,QComboBox) else field.isChecked() if isinstance(field,QCheckBox) else field.value() if isinstance(field,QSpinBox) else field.toPlainText() if isinstance(field,QPlainTextEdit) else field.text()
    def GetValues(self):
        import copy
        value=copy.deepcopy(self.Values)
        for (section,key),field in self.Controls.items():value[section][key]=self._Value(field)
        for (platform,key),field in self.PlatformControls.items():value['platforms'][platform][key]=self._Value(field)
        return Effective(value)
    def _Preset(self):
        preset=self.Controls[('security','preset')].currentData()
        if preset=='custom':return
        value=ApplyPreset(self.GetValues(),preset);self._applying_preset=True
        try:
            for section in ('security','packaging'):
                for key,item in value[section].items():
                    field=self.Controls.get((section,key))
                    if isinstance(field,QComboBox):field.setCurrentIndex(field.findData(item))
                    elif isinstance(field,QSpinBox):field.setValue(item)
                    elif isinstance(field,QCheckBox):field.setChecked(item)
        finally:self._applying_preset=False
    def _Custom(self,*_):
        if not self._applying_preset:
            field=self.Controls[('security','preset')];field.setCurrentIndex(field.findData('custom'))
    def _Apply(self):
        try:Save(self.Project,self.GetValues());self.accept()
        except (OSError,ValueError) as error:self.Error.setText(self.Tr('build.invalid',error=str(error)))
