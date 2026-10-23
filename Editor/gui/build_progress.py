"""Modal build progress and completion dialogs using the editor theme."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog,QVBoxLayout,QHBoxLayout,QLabel,QProgressBar,QPushButton,QPlainTextEdit,QLineEdit
from ..platform_services import RevealFiles

class BuildProgressDialog(QDialog):
    def __init__(self,editor):
        super().__init__(editor);self.Tr=editor.Localization.Translate;self.Active=True
        self.setWindowTitle(self.Tr('build.progress_title'));self.setWindowFlag(Qt.WindowType.WindowCloseButtonHint,False);self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(460,160)
        layout=QVBoxLayout(self);self.Stage=QLabel(self.Tr('build.started'));self.Stage.setWordWrap(True);layout.addWidget(self.Stage)
        self.Progress=QProgressBar();self.Progress.setRange(0,100);self.Progress.setValue(0);layout.addWidget(self.Progress)
        self.Details=QPlainTextEdit();self.Details.setReadOnly(True);self.Details.hide();layout.addWidget(self.Details)
        self.Close=QPushButton(self.Tr('build.ok'));self.Close.clicked.connect(self.accept);self.Close.hide();layout.addWidget(self.Close)
    def SetStage(self,stage):
        self.Stage.setText(self.Tr('build.stage.'+stage));self.Progress.setValue({'scripts':10,'cook':30,'package':70,'verify':90,'complete':100}.get(stage,self.Progress.value()))
    def Fail(self,message):
        self.Active=False;self.Stage.setText(self.Tr('build.failed_title'));self.Details.setPlainText(message);self.Details.show();self.Close.show();self.resize(560,300)
    def Finish(self):self.Active=False;self.accept()
    def reject(self):
        if not self.Active:super().reject()
    def closeEvent(self,event):
        if self.Active:event.ignore()
        else:super().closeEvent(event)

class BuildCompleteDialog(QDialog):
    def __init__(self,editor,path):
        super().__init__(editor);tr=editor.Localization.Translate;self.setWindowTitle(tr('build.complete_title'));self.setWindowModality(Qt.WindowModality.WindowModal);self.setModal(True);self.resize(520,170)
        layout=QVBoxLayout(self);layout.addWidget(QLabel(tr('build.stage.complete')));output=QLineEdit(str(path));output.setReadOnly(True);layout.addWidget(output);self.Error=QLabel();self.Error.setWordWrap(True);layout.addWidget(self.Error)
        row=QHBoxLayout();row.addStretch();reveal=QPushButton(tr('build.show_files'));ok=QPushButton(tr('build.ok'));ok.setDefault(True);row.addWidget(reveal);row.addWidget(ok);layout.addLayout(row);ok.clicked.connect(self.accept)
        def show():
            try:RevealFiles([path])
            except (OSError,ValueError) as error:self.Error.setText(str(error))
        reveal.clicked.connect(show)
