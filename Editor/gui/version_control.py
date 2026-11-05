"""Asynchronous, project-scoped Git tools using the editor's shared theme."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PySide6.QtCore import QSignalBlocker, Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QFontDatabase, QSyntaxHighlighter, QTextCharFormat
from PySide6.QtWidgets import (QAbstractItemView, QComboBox, QDialog, QHBoxLayout, QInputDialog,
                               QHeaderView, QLabel, QMessageBox, QPlainTextEdit, QPushButton, QSplitter,
                               QTabWidget, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget)

from ..version_control import GitError, GitRepository
from .widgets.menu_bar import EditorMenu


class DiffHighlighter(QSyntaxHighlighter):
    def __init__(self, view, themes):
        super().__init__(view.document())
        self.Themes = themes
        themes.ThemeChanged.connect(self.ThemeChanged)

    def ThemeChanged(self, _theme):
        self.rehighlight()

    def highlightBlock(self, text):
        theme = self.Themes.GetTheme()
        color = None
        if text.startswith(("+++", "---", "diff ", "index ")):
            color = theme.text_muted
        elif text.startswith("+"):
            color = theme.success
        elif text.startswith("-"):
            color = theme.error
        elif text.startswith("@@"):
            color = theme.modified
        if color:
            formatting = QTextCharFormat()
            formatting.setForeground(QColor(color))
            self.setFormat(0, self.currentBlock().length() - 1, formatting)


class WorktreeUpdate:
    """Release native scene locks and invalidate document buffers around Git writes."""
    def __init__(self, editor):
        self.Editor = editor
        self.Project = editor.Runtime.ProjectInfo().get("project_path", "")
        self.Scenes = editor.Runtime.LoadedScenes()
        self.TimerActive = editor.Controller.Timer.isActive()

    def Begin(self):
        editor = self.Editor
        if editor.Runtime.IsPlaying() or editor.Controller.IsDirty:
            raise GitError("save_stop_first")
        for panel in (editor.Animation, editor.VFXGraph, editor.ShaderGraph, editor.AudioMixer):
            if not panel.Flush():
                raise GitError("save_stop_first")
        editor.Animation.EndPreview()
        editor.Controller.Timer.stop()
        try:
            editor.Runtime.SetSceneFileLocksSuspended(True)
            for panel in (editor.Animation, editor.VFXGraph, editor.ShaderGraph, editor.AudioMixer):
                panel.ResetDocument()
        except Exception:
            editor.Runtime.SetSceneFileLocksSuspended(False)
            if self.TimerActive:
                editor.Controller.Timer.start()
            raise
        editor.Runtime.ProjectChanging.emit()

    def Finish(self):
        editor = self.Editor
        # Clear clean document buffers before loading another revision. A stale
        # animation/mixer buffer must never autosave over the checked-out file.
        try:
            editor.Runtime.SetSceneFileLocksSuspended(False)
            if not editor.Runtime.LoadProject(self.Project):
                raise GitError("reload_failed", editor.Runtime.LastError())
            active = ""
            for scene in self.Scenes:
                path = scene.get("path", "")
                if path and Path(path).is_file():
                    editor.Runtime.LoadSceneAdditive(path)
                    if scene.get("active"):
                        active = str(Path(path).resolve())
            for scene in editor.Runtime.LoadedScenes():
                if scene.get("path") and str(Path(scene["path"]).resolve()) == active:
                    editor.Runtime.ActivateScene(scene["uuid"])
                    break
            editor.Controller.SelectEntity(None)
        except Exception as error:
            raise GitError("reload_failed", str(error)) from error
        else:
            if self.TimerActive:
                editor.Controller.Timer.start()


class ConflictDialog(QDialog):
    def __init__(self, owner, path, values):
        super().__init__(owner)
        self.setWindowTitle(owner.Tr("vcs.resolve") + " — " + path)
        self.resize(900, 650)
        layout = QVBoxLayout(self)
        tabs = QTabWidget()
        layout.addWidget(tabs, 1)
        for stage in (1, 2, 3):
            view = owner.TextView(highlight=False)
            data = values[stage]
            view.setPlainText(owner.Tr("vcs.deleted_side") if data is None else data.decode("utf-8", "replace"))
            tabs.addTab(view, owner.Tr("vcs.stage_" + str(stage)))
        self.Result = owner.TextView(highlight=False)
        self.Result.setReadOnly(False)
        self.Result.setPlainText(values["working"])
        tabs.addTab(self.Result, owner.Tr("vcs.result"))
        tabs.setCurrentWidget(self.Result)
        buttons = QHBoxLayout()
        layout.addLayout(buttons)
        save = QPushButton(owner.Tr("vcs.save_resolve"))
        save.clicked.connect(self.accept)
        buttons.addWidget(save)
        cancel = QPushButton(owner.Tr("vcs.cancel"))
        cancel.clicked.connect(self.reject)
        buttons.addWidget(cancel)


class VersionControlDialog(QDialog):
    def __init__(self, editor):
        super().__init__(editor)
        self.Editor = editor
        self.Tr = editor.Localization.Translate
        self.ProjectFile = Path(editor.Runtime.ProjectInfo()["project_path"])
        self.Project = self.ProjectFile.parent
        self.Repository = None
        self.State = None
        self.Future = None
        self.Deliver = None
        self.UpdateSession = None
        self.PendingUpdate = None
        self.RecoverySession = None
        self.ReloadFailed = False
        self.ConflictWindow = None
        self.DeferredPreview = None
        self.ActionButtons = []
        self.Executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="BazzaltGit")
        self.setWindowTitle(self.Tr("vcs.title"))
        self.setWindowModality(Qt.WindowModality.WindowModal)
        self.setWindowFlag(Qt.WindowType.WindowMinimizeButtonHint, False)
        self.resize(1080, 720)
        layout = QVBoxLayout(self)
        self.Location = QLabel()
        self.Location.setTextFormat(Qt.TextFormat.PlainText)
        self.Location.setWordWrap(True)
        self.Location.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(self.Location)
        self.Body = QWidget()
        body = QVBoxLayout(self.Body)
        body.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.Body, 1)
        top = QHBoxLayout()
        body.addLayout(top)
        self.RefreshButton = self.Button(top, "refresh", self.Refresh)
        self.InitButton = self.Button(top, "initialize", self.Initialize)
        self.Button(top, "fetch", lambda: self.Mutate("Fetch"))
        self.Button(top, "pull", lambda: self.Mutate("Pull", worktree=True))
        self.Button(top, "push", lambda: self.Mutate("Push"))
        self.Button(top, "stash", lambda: self.Mutate("Stash", worktree=True, confirm=True))
        self.Button(top, "pop_stash", lambda: self.Mutate("PopStash", worktree=True, confirm=True))
        top.addStretch()
        self.Tabs = QTabWidget()
        body.addWidget(self.Tabs, 1)
        changes_page = QWidget()
        changes_layout = QVBoxLayout(changes_page)
        split = QSplitter()
        changes_layout.addWidget(split, 1)
        self.Changes = QTreeWidget()
        self.Changes.setHeaderLabels([self.Tr("vcs.path"), self.Tr("vcs.index"), self.Tr("vcs.worktree")])
        self.Changes.headerItem().setToolTip(1, self.Tr("vcs.status_codes"))
        self.Changes.headerItem().setToolTip(2, self.Tr("vcs.status_codes"))
        self.Changes.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.Changes.header().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.Changes.header().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.Changes.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.Changes.itemSelectionChanged.connect(self.LoadDiff)
        self.Changes.itemDoubleClicked.connect(lambda *_: self.Resolve())
        self.Changes.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.Changes.customContextMenuRequested.connect(self.ChangeMenu)
        split.addWidget(self.Changes)
        preview = QWidget()
        preview_layout = QVBoxLayout(preview)
        self.DiffMode = QComboBox()
        self.DiffMode.addItems([self.Tr("vcs.worktree"), self.Tr("vcs.index")])
        self.DiffMode.currentIndexChanged.connect(self.LoadDiff)
        preview_layout.addWidget(self.DiffMode)
        self.Diff = self.TextView()
        preview_layout.addWidget(self.Diff, 1)
        split.addWidget(preview)
        split.setSizes([410, 650])
        row = QHBoxLayout()
        changes_layout.addLayout(row)
        self.Button(row, "stage", lambda: self.Mutate("Stage", self.Paths()))
        self.Button(row, "unstage", lambda: self.Mutate("Unstage", self.Paths()))
        self.Button(row, "discard", lambda: self.Mutate("Restore", self.Paths(), worktree=True, confirm=True))
        self.Button(row, "resolve", self.Resolve)
        self.Button(row, "ours", lambda: self.ResolveSide(2))
        self.Button(row, "theirs", lambda: self.ResolveSide(3))
        self.Message = QPlainTextEdit()
        self.Message.setPlaceholderText(self.Tr("vcs.commit_message"))
        self.Message.setMaximumHeight(80)
        changes_layout.addWidget(self.Message)
        commit_row = QHBoxLayout()
        changes_layout.addLayout(commit_row)
        self.Button(commit_row, "commit", lambda: self.Mutate("Commit", self.Message.toPlainText()))
        self.Button(commit_row, "abort_merge", lambda: self.Mutate("AbortMerge", worktree=True, confirm=True))
        commit_row.addStretch()
        self.Tabs.addTab(changes_page, self.Tr("vcs.changes"))
        history_split = QSplitter()
        self.History = QTreeWidget()
        self.History.setHeaderLabels([self.Tr("vcs.commit_id"), self.Tr("vcs.author"), self.Tr("vcs.date"), self.Tr("vcs.subject")])
        self.History.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        self.History.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.History.itemSelectionChanged.connect(self.LoadCommit)
        history_split.addWidget(self.History)
        self.HistoryDiff = self.TextView()
        history_split.addWidget(self.HistoryDiff)
        history_split.setSizes([500, 550])
        self.Tabs.addTab(history_split, self.Tr("vcs.history"))
        branches_page = QWidget()
        branches_layout = QVBoxLayout(branches_page)
        self.Branches = QComboBox()
        branches_layout.addWidget(self.Branches)
        branch_row = QHBoxLayout()
        branches_layout.addLayout(branch_row)
        self.Button(branch_row, "switch", lambda: self.Mutate("Switch", self.Branches.currentText(), worktree=True))
        self.Button(branch_row, "create_branch", self.CreateBranch)
        self.Button(branch_row, "merge", lambda: self.Mutate("Merge", self.Branches.currentText(), worktree=True, confirm=True))
        branches_layout.addStretch()
        self.Tabs.addTab(branches_page, self.Tr("vcs.branches"))
        self.Status = QPlainTextEdit()
        self.Status.setReadOnly(True)
        self.Status.setMaximumHeight(88)
        self.Status.setMinimumHeight(44)
        self.Status.setPlaceholderText(self.Tr("vcs.ready"))
        layout.addWidget(self.Status)
        self.ReloadButton = QPushButton(self.Tr("vcs.retry_reload"))
        self.ReloadButton.clicked.connect(self.RetryReload)
        self.ReloadButton.hide()
        layout.addWidget(self.ReloadButton)
        self.Poll = QTimer(self)
        self.Poll.setInterval(40)
        self.Poll.timeout.connect(self.CheckFuture)
        self.Poll.start()
        self.AutoRefresh = QTimer(self)
        self.AutoRefresh.setInterval(5000)
        self.AutoRefresh.timeout.connect(self._AutoRefresh)
        self.AutoRefresh.start()
        self.Refresh()

    @property
    def Busy(self):
        return self.Future is not None

    def Button(self, row, name, callback):
        button = QPushButton(self.Tr("vcs." + name))
        button.clicked.connect(lambda _checked=False: callback())
        row.addWidget(button)
        self.ActionButtons.append(button)
        return button

    def TextView(self, highlight=True):
        view = QPlainTextEdit()
        view.setReadOnly(True)
        view.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        view.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        if highlight:
            view.Highlighter = DiffHighlighter(view, self.Editor.ThemeManager)
        return view

    def Paths(self):
        return [item.data(0, Qt.ItemDataRole.UserRole) for item in self.Changes.selectedItems()]

    def Error(self, error):
        key = error.Key if isinstance(error, GitError) else "failed"
        text = self.Tr("vcs." + key)
        if str(error):
            text += "\n" + str(error)
        self.Status.setPlainText(text)

    def Submit(self, callback, deliver, worktree=False, mutation=False):
        if self.Busy or self.ReloadFailed:
            return
        if worktree:
            session = self.PendingUpdate
            if session is None:
                session = WorktreeUpdate(self.Editor)
                try:
                    session.Begin()
                except Exception as error:
                    self.Error(error)
                    return
            self.UpdateSession = session
        self.Body.setEnabled(not mutation)
        for button in self.ActionButtons:
            button.setEnabled(False)
        if mutation or self.Repository is None:
            self.Status.setPlainText(self.Tr("vcs.busy"))
        self.Deliver = deliver
        self.Future = self.Executor.submit(callback)

    def CheckFuture(self):
        if not self.Busy or not self.Future.done():
            return
        future, deliver = self.Future, self.Deliver
        self.Future = self.Deliver = None
        session, self.UpdateSession = self.UpdateSession, None
        self.Body.setEnabled(True)
        for button in self.ActionButtons:
            button.setEnabled(True)
        self.InitButton.setEnabled(self.Repository is None)
        try:
            result = future.result()
            if session:
                self.PendingUpdate = session
            self.Status.clear()
            deliver(result)
        except Exception as error:
            self.Error(error)
            if session:
                self.ReloadFailed = True
                self.RecoverySession = session
                self.ReloadButton.show()

        preview, self.DeferredPreview = self.DeferredPreview, None
        if preview and not self.ReloadFailed and not self.Busy:
            (self.LoadDiff if preview == "diff" else self.LoadCommit)()

    def Refresh(self):
        def Read():
            repository = GitRepository(self.Project)
            return repository, repository.Snapshot()
        self.Submit(Read, self.Ready)

    def Ready(self, result):
        self.Repository, state = result
        self.ShowState(state)

    def _AutoRefresh(self):
        if self.isVisible() and not self.Busy and self.ConflictWindow is None:
            self.Refresh()

    def ShowState(self, state):
        self.State = state
        self.InitButton.setEnabled(False)
        self.Location.setText(self.Tr("vcs.location", root=state.Root, branch=state.Branch,
                                      upstream=state.Upstream or "—", ahead=state.Ahead, behind=state.Behind))
        if state.Merge or state.Rebase:
            self.Location.setText(self.Location.text() + "\n" + self.Tr("vcs.rebase_active" if state.Rebase else "vcs.merge_active"))
        selected = set(self.Paths())
        history_id = self.History.currentItem().data(0, Qt.ItemDataRole.UserRole) if self.History.currentItem() else None
        blockers = [QSignalBlocker(self.Changes), QSignalBlocker(self.History)]
        self.Changes.clear()
        for change in state.Changes:
            item = QTreeWidgetItem([change.Path, change.Index, change.Worktree])
            item.setData(0, Qt.ItemDataRole.UserRole, change.Path)
            item.setData(0, Qt.ItemDataRole.UserRole + 1, change.Conflict)
            item.setToolTip(0, (change.Original + " → " if change.Original else "") + change.Path)
            if change.Conflict:
                item.setForeground(0, QBrush(QColor(self.Editor.ThemeManager.GetTheme().error)))
            self.Changes.addTopLevelItem(item)
            item.setSelected(change.Path in selected)
        self.History.clear()
        for identity, author, date, subject in state.History:
            item = QTreeWidgetItem([identity[:10], author, date[:16].replace("T", " "), subject])
            item.setToolTip(2, date)
            item.setData(0, Qt.ItemDataRole.UserRole, identity)
            self.History.addTopLevelItem(item)
            if identity == history_id:
                self.History.setCurrentItem(item)
        previous = self.Branches.currentText()
        self.Branches.clear()
        self.Branches.addItems(list(state.Branches))
        self.Branches.setCurrentText(previous if previous in state.Branches else state.Branch)
        del blockers
        if self.Paths():
            self.DeferredPreview = "diff"
        else:
            self.Diff.clear()

    def Initialize(self):
        if QMessageBox.question(self, self.Tr("vcs.title"), self.Tr("vcs.confirm_initialize")) != QMessageBox.StandardButton.Yes:
            return
        self.Submit(lambda: GitRepository.Initialize(self.Project), lambda _repo: self.Refresh(), mutation=True)

    def Mutate(self, method, *args, worktree=False, confirm=False):
        if self.Repository is None:
            self.Error(GitError("no_repository"))
            return
        if confirm and QMessageBox.question(self, self.Tr("vcs.title"), self.Tr("vcs.confirm_" + method.lower())) != QMessageBox.StandardButton.Yes:
            return
        def Run():
            error = None
            try:
                if method == "Switch" and len(args) == 1:
                    self.Repository.VerifyProject(args[0], self.ProjectFile)
                getattr(self.Repository, method)(*args)
            except Exception as failure:
                error = failure
            return self.Repository.Snapshot(), error
        def Done(result):
            state, error = result
            conflicted = any(change.Conflict for change in state.Changes)
            if self.PendingUpdate and not conflicted:
                self.PendingUpdate.Finish()
                self.PendingUpdate = None
            self.ShowState(state)
            if method == "Commit" and error is None:
                self.Message.clear()
            if error is not None:
                self.Error(error)
            elif conflicted and self.PendingUpdate:
                self.Status.setPlainText(self.Tr("vcs.resolve_first"))
        self.Submit(Run, Done, worktree or self.PendingUpdate is not None, mutation=True)

    def CreateBranch(self):
        name, accepted = QInputDialog.getText(self, self.Tr("vcs.create_branch"), self.Tr("vcs.branch_name"))
        if accepted:
            self.Mutate("Switch", name, True, worktree=True)

    def LoadDiff(self):
        paths = self.Paths()
        if self.Busy:
            self.DeferredPreview = "diff"
            return
        if self.Repository and paths and not self.Busy:
            staged = self.DiffMode.currentIndex() == 1
            self.Diff.clear()
            self.Submit(lambda: self.Repository.Diff(paths[0], staged), lambda value: self.ShowDiff(self.Diff, value))

    def LoadCommit(self):
        item = self.History.currentItem()
        if self.Busy:
            self.DeferredPreview = "commit"
            return
        if item and self.Repository and not self.Busy:
            identity = item.data(0, Qt.ItemDataRole.UserRole)
            self.Submit(lambda: self.Repository.ShowCommit(identity), lambda value: self.ShowDiff(self.HistoryDiff, value))

    def ShowDiff(self, view, value):
        text = value["text"] or self.Tr("vcs.no_diff")
        if value["truncated"]:
            text += "\n" + self.Tr("vcs.truncated")
        view.setPlainText(text)

    def Resolve(self):
        paths = self.Paths()
        if len(paths) != 1 or self.Repository is None:
            return
        path = paths[0]
        self.Submit(lambda: self.Repository.Conflict(path), lambda value: self.EditConflict(path, value))

    def EditConflict(self, path, values):
        self.ConflictWindow = ConflictDialog(self, path, values)
        accepted = self.ConflictWindow.exec()
        text = self.ConflictWindow.Result.toPlainText()
        self.ConflictWindow.deleteLater()
        self.ConflictWindow = None
        if accepted:
            self.Mutate("ResolveText", path, text, worktree=True)

    def ResolveSide(self, stage):
        paths = self.Paths()
        if len(paths) == 1 and QMessageBox.question(self, self.Tr("vcs.resolve"), self.Tr("vcs.confirm_side")) == QMessageBox.StandardButton.Yes:
            self.Mutate("ResolveSide", paths[0], stage, worktree=True)

    def ChangeMenu(self, position):
        if not self.Paths():
            return
        menu = EditorMenu("", self.Editor.ThemeManager, self)
        for name, callback in (("stage", lambda: self.Mutate("Stage", self.Paths())),
                               ("unstage", lambda: self.Mutate("Unstage", self.Paths())),
                               ("resolve", self.Resolve),
                               ("discard", lambda: self.Mutate("Restore", self.Paths(), worktree=True, confirm=True))):
            menu.addAction(self.Tr("vcs." + name)).triggered.connect(lambda _=False, action=callback: action())
        for action in menu.actions():
            action.setEnabled(not self.Busy)
        menu.exec(self.Changes.viewport().mapToGlobal(position))
        menu.deleteLater()

    def RetryReload(self):
        try:
            self.RecoverySession.Finish()
            self.ReloadFailed = False
            self.PendingUpdate = None
            self.RecoverySession = None
            self.ReloadButton.hide()
            self.Refresh()
        except Exception as error:
            self.Error(error)

    def reject(self):
        if self.Busy or self.ReloadFailed or self.PendingUpdate:
            key = "busy" if self.Busy else "reload_failed" if self.ReloadFailed else "resolve_first"
            self.Status.setPlainText(self.Tr("vcs." + key))
            return
        super().reject()

    def Shutdown(self):
        self.AutoRefresh.stop()
        self.Poll.stop()
        self.Executor.shutdown(wait=False, cancel_futures=True)
