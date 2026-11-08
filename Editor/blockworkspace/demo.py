"""Runnable demo:  python -m blockworkspace.demo

Shows the toolbox, Scratch-like blocks, nested blocks, IF/ELIF/ELSE mutation, multi-selection, context menus,
undo/redo, workspace-XML import/export and live generated XML (or JSON / pseudo code)."""
from __future__ import annotations
import sys

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QFont, QKeySequence
from PySide6.QtWidgets import (QApplication, QMainWindow, QPlainTextEdit, QTabWidget, QDockWidget, QWidget, QVBoxLayout,
                               QHBoxLayout, QPushButton, QComboBox, QLabel, QListWidget, QToolBar, QFileDialog,
                               QMessageBox)

from . import BlockWorkspace, BlockTheme, NEXT
from .examples import GAME_BLOCKS, GAME_ASSETS, GAME_ENTITIES


class DemoWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("BlockWorkspace demo — native PySide6 visual programming")
        self.resize(1480, 900)

        # ------------------------------------------------------------------ the whole public API in 3 lines
        self.Workspace = BlockWorkspace(self)
        self.Workspace.RegisterBlocks(GAME_BLOCKS)          # appear in the toolbox automatically
        self.setCentralWidget(self.Workspace)
        # ------------------------------------------------------------------------------------------------
        for k, v in GAME_ASSETS.items():
            self.Workspace.SetAssets(k, v)
        for k, v in GAME_ENTITIES.items():
            self.Workspace.SetEntities(k, v)
        self.Workspace.AddCategory("Movement", "#4C97FF", Order=1.1)
        self.Workspace.AddCategory("Sensing", "#5CB1D6", Order=1.2)
        self.Workspace.AddCategory("Combat", "#FF6680", Order=1.3)

        self._BuildDocks()
        self._BuildToolbar()
        self.Workspace.AddWorkspaceAction("Load Sample Behavior", lambda ws: self.LoadSample())

        self._timer = None
        self.Workspace.DocumentChanged.connect(self._RefreshGenerated)
        self.Workspace.SelectionChanged.connect(self._UpdateStatus)
        self.Workspace.ZoomChanged.connect(lambda z: self._UpdateStatus())
        self.Workspace.ValidationChanged.connect(self._ShowIssues)
        self.LoadSample()

    # ------------------------------------------------------------------------------------------------ sample
    def LoadSample(self):
        ws = self.Workspace
        with ws.Document.Transaction("Load Sample"):
            ws.Clear()
            hat = ws.CreateBlock("core.event.WhenStarted", 70, 70)
            forever = ws.CreateBlock("core.control.Forever")
            iff = ws.CreateBlock("core.control.If")
            ws.AddSection(iff, "Elif")
            ws.AddSection(iff, "Else")
            keys = {}
            for ri in iff.Inputs():
                if ri.Section:
                    keys[(ri.Section, ri.Def.Name)] = ri.Key
            c1 = ws.CreateBlock("game.ai.CanSee", Values={"Target": "Player"})
            c2 = ws.CreateBlock("core.logic.Compare", Values={"Op": "lt", "B": 5})
            dist = ws.CreateBlock("game.ai.DistanceTo", Values={"Target": "Player"})
            move = ws.CreateBlock("game.ai.MoveTo", Values={"Target": "Player"})
            attack = ws.CreateBlock("game.ai.Attack", Values={"Target": "Player"})
            alert = ws.CreateBlock("game.ai.SetAlert", Values={"Level": 80})
            patrol = ws.CreateBlock("game.ai.Patrol", Values={"Route": "Route01"})
            wait = ws.CreateBlock("core.control.Wait", Values={"Seconds": 2})
            ws.Connect(hat, NEXT, forever)
            ws.Connect(forever, "Body", iff)
            ws.Connect(iff, "Condition", c1)
            ws.Connect(iff, "Then", move)
            ws.Connect(move, NEXT, attack)
            ws.Connect(iff, keys[("Elif", "Condition")], c2)
            ws.Connect(c2, "A", dist)
            ws.Connect(iff, keys[("Elif", "Do")], alert)
            ws.Connect(iff, keys[("Else", "Do")], patrol)
            ws.Connect(patrol, NEXT, wait)
            ws.Document.SetComment(move, "Chase the player while visible.", True)
            # loose blocks to play with
            ws.CreateBlock("game.ai.Attack", 520, 90, Values={"Target": "Enemy"})
            ws.CreateBlock("core.logic.And", 520, 190)
            ws.CreateBlock("game.ai.Switch", 520, 270)
        ws.Commands.Clear()
        ws.Selection.Clear()
        self.Workspace.ZoomToFit()
        self._RefreshGenerated()

    # ------------------------------------------------------------------------------------------------ UI
    def _BuildToolbar(self):
        ws = self.Workspace
        tb = QToolBar("Main")
        tb.setMovable(False)
        self.addToolBar(tb)

        def act(text, fn, key=None, checkable=False, checked=False):
            a = QAction(text, self)
            a.triggered.connect(lambda v=False: fn(v) if checkable else fn())
            if key:
                a.setShortcut(QKeySequence(key))
            if checkable:
                a.setCheckable(True)
                a.setChecked(checked)
            tb.addAction(a)
            return a

        act("↶ Undo", ws.Undo)
        act("↷ Redo", ws.Redo)
        tb.addSeparator()
        act("Fit", ws.ZoomToFit)
        act("100%", ws.ResetZoom)
        act("Grid", ws.SetGridVisible, checkable=True, checked=True)
        act("Snap to grid", ws.SetSnapToGrid, checkable=True)
        tb.addSeparator()
        act("Sample", self.LoadSample)
        act("Clear", ws.Clear)
        tb.addSeparator()
        act("Save XML…", self._SaveFile)
        act("Load XML…", self._LoadFile)
        tb.addSeparator()
        self.ThemeBox = QComboBox()
        self.ThemeBox.addItems(["Scratch (light)", "Dark", "High contrast"])
        self.ThemeBox.currentIndexChanged.connect(self._ThemeChanged)
        tb.addWidget(QLabel("  Theme: "))
        tb.addWidget(self.ThemeBox)
        self.Status = self.statusBar()
        self._UpdateStatus()

    def _BuildDocks(self):
        mono = QFont("Menlo, Consolas, DejaVu Sans Mono")
        mono.setStyleHint(QFont.Monospace)
        mono.setPointSize(10)
        tabs = QTabWidget()
        # --- generated code
        gen = QWidget()
        gl = QVBoxLayout(gen)
        row = QHBoxLayout()
        row.addWidget(QLabel("Target:"))
        self.TargetBox = QComboBox()
        self.TargetBox.addItems(self.Workspace.CodeGenerator.TargetNames())
        self.TargetBox.currentIndexChanged.connect(self._RefreshGenerated)
        row.addWidget(self.TargetBox)
        row.addStretch(1)
        gl.addLayout(row)
        self.GenView = QPlainTextEdit()
        self.GenView.setReadOnly(True)
        self.GenView.setFont(mono)
        self.GenView.setLineWrapMode(QPlainTextEdit.NoWrap)
        gl.addWidget(self.GenView)
        tabs.addTab(gen, "Generated code")
        # --- workspace xml
        wx = QWidget()
        wl = QVBoxLayout(wx)
        row = QHBoxLayout()
        b1, b2 = QPushButton("Export ↓"), QPushButton("Import ↑ (replaces workspace)")
        b1.clicked.connect(self._Export)
        b2.clicked.connect(self._Import)
        row.addWidget(b1)
        row.addWidget(b2)
        wl.addLayout(row)
        self.XmlView = QPlainTextEdit()
        self.XmlView.setFont(mono)
        self.XmlView.setLineWrapMode(QPlainTextEdit.NoWrap)
        wl.addWidget(self.XmlView)
        tabs.addTab(wx, "Workspace XML")
        # --- issues
        self.IssueList = QListWidget()
        tabs.addTab(self.IssueList, "Validation")
        dock = QDockWidget("Output", self)
        dock.setWidget(tabs)
        dock.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        self.addDockWidget(Qt.RightDockWidgetArea, dock)
        dock.setMinimumWidth(430)
        self.Tabs = tabs

    # ------------------------------------------------------------------------------------------------ slots
    def _RefreshGenerated(self, *_):
        try:
            self.GenView.setPlainText(self.Workspace.GenerateCode(self.TargetBox.currentText()))
        except Exception as e:  # keep the demo alive while a custom generator is being developed
            self.GenView.setPlainText(f"Generation error: {e}")
        self._UpdateStatus()

    def _UpdateStatus(self, *_):
        ws = self.Workspace
        self.Status.showMessage(f"{len(ws.Blocks())} blocks   |   {ws.Selection.Count()} selected   |   zoom {ws.Canvas.Zoom * 100:.0f}%   |   "
                                f"undo: {ws.Commands.UndoText or '—'}   redo: {ws.Commands.RedoText or '—'}")

    def _ShowIssues(self, issues):
        self.IssueList.clear()
        for i in issues:
            b = self.Workspace.FindBlock(i.BlockId)
            self.IssueList.addItem(f"[{i.Severity}] {b.Definition.DisplayName if b else '?'}: {i.Message}")
        self.Tabs.setTabText(2, f"Validation ({len(issues)})" if issues else "Validation")

    def _Export(self):
        self.XmlView.setPlainText(self.Workspace.ExportWorkspaceXml(IncludeView=True))
        self.Tabs.setCurrentIndex(1)

    def _Import(self):
        try:
            rep = self.Workspace.ImportWorkspaceXml(self.XmlView.toPlainText(), Undoable=True)
        except Exception as e:
            QMessageBox.warning(self, "Import failed", str(e))
            return
        if rep.UnknownTypes:
            QMessageBox.information(self, "Unknown block types", "Preserved as UnknownBlock:\n" + "\n".join(sorted(rep.UnknownTypes)))

    def _SaveFile(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save workspace", "behavior.workspace.xml", "XML (*.xml)")
        if path:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.Workspace.ExportWorkspaceXml(IncludeView=True))

    def _LoadFile(self):
        path, _ = QFileDialog.getOpenFileName(self, "Load workspace", "", "XML (*.xml)")
        if path:
            with open(path, "r", encoding="utf-8") as f:
                self.XmlView.setPlainText(f.read())
            self._Import()

    def _ThemeChanged(self, i):
        self.Workspace.SetTheme([BlockTheme.Light, BlockTheme.Dark, BlockTheme.HighContrast][i]())


def main():
    app = QApplication.instance() or QApplication(sys.argv)
    w = DemoWindow()
    w.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
