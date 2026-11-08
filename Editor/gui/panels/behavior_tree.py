"""Block behavior authoring and explicitly unlocked XML, using editor history routing."""
from pathlib import Path
from PySide6.QtCore import Signal, QTimer
from PySide6.QtGui import QUndoCommand
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QPushButton,QLabel,QPlainTextEdit,QTabWidget
from ... import behavior_tree as Model
from ...blockworkspace import BlockWorkspace,BlockTheme

class SourceChange(QUndoCommand):
    def __init__(self,panel,before,after):
        super().__init__('Edit behavior XML');self.Panel=panel;self.Before=before;self.After=after
    def undo(self):self.Panel.SetSource(self.Before)
    def redo(self):self.Panel.SetSource(self.After)

class BehaviorTreePanel(QWidget):
    AssetChanged=Signal(object)
    def __init__(self,runtime,locale,resources,themes,parent=None):
        super().__init__(parent);self.Path=None;self.Root=None;self.Stamp=None;self.Saved='';self._loading=False
        self.Locale=locale;self.Themes=themes
        layout=QVBoxLayout(self);bar=QHBoxLayout();layout.addLayout(bar)
        self.Title=QLabel();bar.addWidget(self.Title,1)
        self.UnlockButton=QPushButton();bar.addWidget(self.UnlockButton);self.UnlockButton.clicked.connect(self.Unlock)
        self.SaveButton=QPushButton();bar.addWidget(self.SaveButton);self.SaveButton.clicked.connect(self.Flush)
        self.ReloadButton=QPushButton();bar.addWidget(self.ReloadButton);self.ReloadButton.clicked.connect(self.Reload)
        self.Tabs=QTabWidget();layout.addWidget(self.Tabs,1)
        self.Workspace=BlockWorkspace(RegisterStandardBlocks=False);Model.RegisterBlocks(self.Workspace);self.Workspace.Toolbox.Refresh()
        for action in ('Copy','Paste','Duplicate','Delete','SelectAll'):self.Workspace.Actions[action].setShortcuts([])
        self.History=self.Workspace.Commands.History
        self.Source=QPlainTextEdit();self.Source.setUndoRedoEnabled(False)
        self.Tabs.addTab(self.Workspace,'');self.Tabs.addTab(self.Source,'')
        self.Error=QLabel();self.Error.setWordWrap(True);layout.addWidget(self.Error)
        self.Workspace.DocumentChanged.connect(self.Changed)
        self.Source.textChanged.connect(self.SourceChanged)
        self.Tabs.currentChanged.connect(self.TabChanged)
        self.Timer=QTimer(self);self.Timer.setSingleShot(True);self.Timer.setInterval(350);self.Timer.timeout.connect(self.Flush)
        themes.ThemeChanged.connect(self.ApplyTheme);locale.LocaleChanged.connect(self.Retranslate)
        self.ApplyTheme();self.Retranslate();self.Workspace.SetReadOnly(True);self.Source.setReadOnly(True)
    def Retranslate(self,*_):
        tr=self.Locale.Translate
        self.UnlockButton.setText(tr('behavior.unlock'));self.SaveButton.setText(tr('behavior.save'));self.ReloadButton.setText(tr('behavior.reload'))
        self.Tabs.setTabText(0,tr('behavior.blocks'));self.Tabs.setTabText(1,'XML')
        Model.LocalizeBlocks(self.Workspace,self.Locale)
        self.Workspace.Toolbox.Search.setPlaceholderText(tr('behavior.search'))
    def showEvent(self,event):
        super().showEvent(event);QTimer.singleShot(0,self.Workspace.ZoomToFit)
    def ApplyTheme(self,*_):
        t=self.Themes.GetTheme()
        self.Workspace.SetTheme(BlockTheme(Name='Bazzalt',FontFamily=self.font().family(),Background=t.background,GridMinor=t.surface_alt,GridMajor=t.surface,
            FieldFill=t.input_background,FieldText=t.text,FieldPlaceholder=t.text_muted,ToolboxBackground=t.surface,
            ToolboxText=t.text,ToolboxSelected=t.selection,ToolboxBorder=t.border,FlyoutBackground=t.surface_alt,
            SearchBackground=t.input_background,SelectionColor=t.border_focus,WarningColor=t.warning,ErrorColor=t.error,
            CategoryColors={'Composites':t.axis_z,'Decorators':t.axis_w,'Utility':t.warning,'Blackboard':t.axis_y,'Physics':t.axis_x,'Navigation':t.modified,'Actions':t.accent}))
    def FileStamp(self):
        if self.Path and self.Path.exists():return self.Path.stat().st_mtime_ns,self.Path.stat().st_size
    def SetSource(self,source):
        self._loading=True;self.Source.setPlainText(source);self._source=source;self._loading=False
        self.Timer.start()
    def SourceChanged(self):
        if self._loading or self.Source.isReadOnly():return
        after=self.Source.toPlainText();self.History.push(SourceChange(self,self._source,after))
    def Changed(self):
        if not self._loading:self.Timer.start()
    def TabChanged(self,index):
        if self._loading or not self.Path:return
        if index==1 and self.Root.get('locked','true')=='true':
            try:self.SetSource(Model.Source(Model.FromWorkspace(self.Workspace,self.Root)))
            except ValueError as e:self.Error.setText(str(e))
    def Open(self,path,force=False):
        if not self.Flush():return False
        try:root=Model.Load(path)
        except (OSError,ValueError) as e:self.Error.setText(str(e));return False
        self.Timer.stop();self.Path=Path(path);self.Root=root;self.Saved=self.Path.read_text(encoding='utf-8');self.Stamp=self.FileStamp()
        self._loading=True
        try:
            self.Workspace.Serializer.ImportWorkspaceXml(Model.ToWorkspace(root),Clear=True,Undoable=False)
            locked=root.get('locked','true')=='true';self.Workspace.SetReadOnly(not locked);self.Source.setReadOnly(locked)
            self.Source.setPlainText(self.Saved);self._source=self.Saved;self.Tabs.setCurrentIndex(0 if locked else 1)
            self.UnlockButton.setEnabled(locked);self.Title.setText(self.Path.name);self.History.clear();self.Error.clear()
        finally:self._loading=False
        return True
    def Unlock(self):
        if not self.Path or not self.Flush():return
        before=Model.Source(self.Root);self.Root.set('locked','false');after=Model.Source(self.Root)
        # Conversion is a document command, including the authoring mode.
        panel=self
        class Conversion(QUndoCommand):
            def __init__(self):super().__init__('Unlock behavior XML')
            def apply(self,source):
                panel.Root=Model.Parse(source);locked=panel.Root.get('locked')=='true'
                panel.Workspace.SetReadOnly(not locked);panel.Source.setReadOnly(locked);panel.UnlockButton.setEnabled(locked)
                panel.SetSource(source);panel.Tabs.setCurrentIndex(0 if locked else 1)
            def undo(self):self.apply(before)
            def redo(self):self.apply(after)
        self.History.push(Conversion())
    def Flush(self):
        self.Timer.stop()
        if not self.Path:return True
        try:
            source=Model.Source(Model.FromWorkspace(self.Workspace,self.Root)) if self.Root.get('locked','true')=='true' else self.Source.toPlainText()
            root=Model.Parse(source)
            if self.Root.get('locked','true')=='false' and root.get('locked','true')!='false':raise ValueError('Unlocked XML must keep locked="false"; reopen a locked file to import blocks')
            if source==self.Saved:return True
            if self.FileStamp()!=self.Stamp:raise ValueError(self.Locale.Translate('graph.external_change'))
            Model.Save(self.Path,source);self.Root=root;self.Saved=source;self.Stamp=self.FileStamp();self.Error.clear();self.AssetChanged.emit(self.Path);return True
        except (OSError,ValueError) as e:self.Error.setText(str(e));return False
    def Reload(self):
        if self.Path:
            path=self.Path;self.Path=None;self.Open(path)
    def ResetDocument(self):
        self.Timer.stop();self.Path=None;self.Root=None;self.Saved='';self._loading=True
        self.Workspace.Clear();self.Source.clear();self.History.clear();self.Title.clear();self.Error.clear();self._loading=False
        self.Workspace.SetReadOnly(True);self.Source.setReadOnly(True)
