"""Project-owned game build configuration. Uses the editor's existing theme."""
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog,QVBoxLayout,QHBoxLayout,QListWidget,QStackedWidget,QWidget,QFormLayout,QScrollArea,QLineEdit,QCheckBox,QComboBox,QSpinBox,QPushButton,QLabel,QPlainTextEdit
from Compiler.settings import Load,Save,ApplyPreset,Validate

class BuildSettingsDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Editor=editor;self.Tr=editor.Localization.Translate;self.Project=Path(editor.Runtime.ProjectInfo()['project_path']);self.Values=Load(self.Project);self.Controls={}
        self.setWindowTitle(self.Tr('build.settings'));self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(760,520);self.setMinimumSize(600,400)
        root=QVBoxLayout(self);body=QHBoxLayout();self.Sections=QListWidget();self.Sections.setFixedWidth(160);self.Pages=QStackedWidget();body.addWidget(self.Sections);body.addWidget(self.Pages,1);root.addLayout(body,1)
        for section,keys in (('production',tuple(self.Values['production'])),('target',tuple(self.Values['target'])),('window',tuple(self.Values['window'])),('intro',tuple(self.Values['intro'])),('packaging',('archive',)),('security',tuple(self.Values['security'])+('format','chunk_mb','decoy_kb'))):
            self.Sections.addItem(self.Tr('build.section.'+section));page=QWidget();form=QFormLayout(page);form.setContentsMargins(14,12,14,12);form.setHorizontalSpacing(20);form.setVerticalSpacing(10);scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);self.Pages.addWidget(scroll)
            options={'architecture':('x64','x86'),'mode':('windowed','maximized','borderless','fullscreen'),'backend':('automatic','vulkan','opengl'),'format':('pak','loose'),'preset':('low','medium','high','very_high','custom'),'encryption':('none','aes-256-gcm'),'key_storage':('embedded','file','environment')}
            for key in keys:
                storage='packaging' if key in ('format','chunk_mb','decoy_kb') else section
                value=self.Values[storage][key]
                if key in options:
                    field=QComboBox()
                    for option in options[key]:field.addItem(self.Tr('build.option.'+option),option)
                    field.setCurrentIndex(field.findData(value))
                elif isinstance(value,bool):field=QCheckBox();field.setChecked(value)
                elif isinstance(value,int):
                    field=QSpinBox();low,high=(320,16384) if key=='width' else (200,16384) if key=='height' else (4,256) if key=='chunk_mb' else (0,30000) if key=='duration_ms' else (48,512) if key=='logo_size' else (0,1024);field.setRange(low,high);field.setValue(value)
                elif key in ('license','description'):field=QPlainTextEdit(str(value));field.setMaximumHeight(90)
                else:field=QLineEdit(str(value))
                self.Controls[(storage,key)]=field;form.addRow(self.Tr('build.field.'+key),field)
        self.Controls[('security','preset')].activated.connect(self._Preset)
        self._applying_preset=False
        for pair in (('security','encryption'),('security','key_storage'),('packaging','format'),('packaging','chunk_mb'),('packaging','decoy_kb')):
            field=self.Controls[pair]
            (field.currentIndexChanged if isinstance(field,QComboBox) else field.valueChanged).connect(self._Custom)
        self.Sections.currentRowChanged.connect(self.Pages.setCurrentIndex);self.Sections.setCurrentRow(0)
        self.Error=QLabel();self.Error.setWordWrap(True);root.addWidget(self.Error)
        buttons=QHBoxLayout();buttons.addStretch();cancel=QPushButton(self.Tr('preferences.cancel'));apply=QPushButton(self.Tr('preferences.apply'));buttons.addWidget(cancel);buttons.addWidget(apply);root.addLayout(buttons);cancel.clicked.connect(self.reject);apply.clicked.connect(self._Apply)
    def GetValues(self):
        value=Load(self.Project)
        for (section,key),field in self.Controls.items():value[section][key]=field.currentData() if isinstance(field,QComboBox) else field.isChecked() if isinstance(field,QCheckBox) else field.value() if isinstance(field,QSpinBox) else field.toPlainText() if isinstance(field,QPlainTextEdit) else field.text()
        return value
    def _Preset(self):
        preset=self.Controls[('security','preset')].currentData()
        if preset=='custom':return
        value=ApplyPreset(self.GetValues(),preset)
        self._applying_preset=True
        try:
            for section in ('security','packaging'):
                for key,item in value[section].items():
                    field=self.Controls[(section,key)]
                    if isinstance(field,QComboBox):field.setCurrentIndex(field.findData(item))
                    elif isinstance(field,QSpinBox):field.setValue(item)
        finally:self._applying_preset=False
    def _Custom(self):
        if not self._applying_preset:
            field=self.Controls[('security','preset')];field.setCurrentIndex(field.findData('custom'))
    def _Apply(self):
        try:Save(self.Project,self.GetValues());self.accept()
        except (OSError,ValueError) as error:self.Error.setText(self.Tr('build.invalid',error=str(error)))
