"""On-demand CPU capture. No polling or sample copying during normal editing."""
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QPushButton,
    QTreeWidget, QTreeWidgetItem, QLabel, QFileDialog)


class ProfilerDialog(QDialog):
    def __init__(self, editor):
        super().__init__(editor)
        self.Runtime = editor.Runtime
        self.Tr = editor.Localization.Translate
        self.Capturing = False
        self.setWindowTitle(self.Tr('profiler.title'))
        self.resize(720, 420)
        layout = QVBoxLayout(self)
        bar = QHBoxLayout()
        self.Start = QPushButton(self.Tr('profiler.start'))
        self.Stop = QPushButton(self.Tr('profiler.stop'))
        self.Export = QPushButton(self.Tr('profiler.export'))
        self.Stop.setEnabled(False)
        for button in (self.Start, self.Stop, self.Export):
            bar.addWidget(button)
        bar.addStretch()
        layout.addLayout(bar)
        self.Table = QTreeWidget()
        self.Table.setHeaderLabels([self.Tr('profiler.scope'), self.Tr('profiler.calls'),
            self.Tr('profiler.total'), self.Tr('profiler.maximum')])
        layout.addWidget(self.Table)
        self.Status = QLabel()
        layout.addWidget(self.Status)
        self.Start.clicked.connect(self.Begin)
        self.Stop.clicked.connect(self.End)
        self.Export.clicked.connect(self.Save)

    def Begin(self):
        try:
            self.Runtime.ProfileCommand('begin_profile_capture')
            self.Capturing = True
            self.Start.setEnabled(False)
            self.Stop.setEnabled(True)
            self.Status.setText(self.Tr('profiler.capturing'))
        except Exception as error:
            self.Status.setText(str(error))

    def End(self):
        try:
            self.Runtime.ProfileCommand('end_profile_capture')
            self.Capturing = False
            self.Start.setEnabled(True)
            self.Stop.setEnabled(False)
            groups = {}
            for sample in self.Runtime.ProfileCommand('profile_samples'):
                stats = groups.setdefault(sample['name'], [0, 0., 0.])
                duration = sample['duration_us'] / 1000
                stats[0] += 1
                stats[1] += duration
                stats[2] = max(stats[2], duration)
            self.Table.clear()
            for name, (count, total, maximum) in sorted(groups.items(), key=lambda item: -item[1][1]):
                QTreeWidgetItem(self.Table, [name, str(count), f'{total:.3f}', f'{maximum:.3f}'])
            self.Status.setText(self.Tr('profiler.finished'))
        except Exception as error:
            self.Status.setText(str(error))

    def Save(self):
        if self.Capturing:
            self.End()
        path, _ = QFileDialog.getSaveFileName(self, self.Tr('profiler.export'), 'bazzalt-trace.json', 'JSON (*.json)')
        if path:
            try:
                self.Runtime.ProfileCommand('export_profile_trace', path)
            except Exception as error:
                self.Status.setText(str(error))

    def done(self, result):
        if self.Capturing:
            self.End()
        super().done(result)
