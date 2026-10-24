"""Editor-only project metadata editing; never reloads or resets open scenes."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QTextEdit, QVBoxLayout


class ProjectSettingsDialog(QDialog):
    def __init__(self, editor):
        super().__init__(editor)
        self.Editor = editor
        self.Tr = editor.Localization.Translate
        self.Info = editor.Runtime.ProjectInfo()
        self.setWindowTitle(self.Tr("project_settings.title"))
        self.setWindowModality(Qt.WindowModality.WindowModal)
        self.setModal(True)
        self.resize(560, 420)
        root = QVBoxLayout(self)
        form = QFormLayout()
        form.setHorizontalSpacing(18)
        form.setVerticalSpacing(10)
        root.addLayout(form)
        self.Name = QLineEdit(self.Info.get("name", ""))
        form.addRow(self.Tr("project_settings.name"), self.Name)
        self.Fields = {}
        properties = dict(self.Info.get("properties", {}))
        for key in ("company", "version", "description"):
            field = QTextEdit() if key == "description" else QLineEdit()
            value = properties.get("project." + key, "")
            if key == "description":
                field.setPlainText(value)
                field.setMaximumHeight(90)
            else:
                field.setText(value)
            self.Fields[key] = field
            form.addRow(self.Tr("project_settings." + key), field)
        for key in ("uuid", "project_path", "asset_directory", "startup_scene"):
            field = QLineEdit(str(self.Info.get(key, "")))
            field.setReadOnly(True)
            field.setToolTip(field.text())
            form.addRow(self.Tr("project_settings." + key), field)
        self.Error = QLabel()
        self.Error.setWordWrap(True)
        root.addWidget(self.Error)
        buttons = QHBoxLayout()
        buttons.addStretch()
        cancel = QPushButton(self.Tr("preferences.cancel"))
        self.Apply = QPushButton(self.Tr("preferences.apply"))
        self.Apply.setDefault(True)
        buttons.addWidget(cancel)
        buttons.addWidget(self.Apply)
        root.addLayout(buttons)
        cancel.clicked.connect(self.reject)
        self.Apply.clicked.connect(self._Apply)
        self.Name.textChanged.connect(self._Validate)
        self._Validate()
        if not self.Info:
            self.Error.setText(self.Tr("project_settings.unavailable"))

    def _Validate(self):
        self.Apply.setEnabled(bool(self.Info and self.Name.text().strip()))

    def _Apply(self):
        properties = dict(self.Info.get("properties", {}))
        for key, field in self.Fields.items():
            properties["project." + key] = field.toPlainText() if key == "description" else field.text()
        if self.Editor.Runtime.UpdateProjectInfo(self.Name.text().strip(), properties):
            self.accept()
        else:
            self.Error.setText(self.Tr("project_settings.save_failed") + " " + self.Editor.Runtime.LastError())
