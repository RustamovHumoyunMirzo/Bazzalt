from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtGui import QAction,QKeySequence
from PySide6.QtWidgets import QWidget,QVBoxLayout,QHBoxLayout,QListWidget,QListWidgetItem,QPushButton,QDialog,QDialogButtonBox,QFormLayout,QLineEdit,QMenu,QMessageBox
from ...macros import MacroStore
from ..widgets.menu_bar import EditorMenu
from ..widgets.fields import AssetPickerInput
from bazzalt.extensions import AtomicJson

class MacrosPanel(QWidget):
    def __init__(self,editor):
        super().__init__();self.Editor=editor;self.Store=MacroStore();self.Shortcuts={};layout=QVBoxLayout(self);bar=QHBoxLayout();self.Buttons=[]
        for key,callback in (('macros.new',self.Create),('macros.run',self.Run),('macros.edit',self.Edit),('macros.delete',self.Delete)):
            button=QPushButton(editor.Localization.Translate(key));button.clicked.connect(callback);bar.addWidget(button);self.Buttons.append((button,key))
        layout.addLayout(bar);self.List=QListWidget();self.List.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu);self.List.customContextMenuRequested.connect(self.Menu);self.List.itemDoubleClicked.connect(lambda *_:self.Run());layout.addWidget(self.List);editor.Localization.LocaleChanged.connect(self.Retranslate);self.Refresh()
    def Retranslate(self,*_):
        for button,key in self.Buttons:button.setText(self.Editor.Localization.Translate(key))
    def Current(self):
        item=self.List.currentItem();return item.data(Qt.ItemDataRole.UserRole) if item else None
    def Refresh(self,identity=None):
        for action in self.Shortcuts.values():self.Editor.removeAction(action);action.setShortcut(QKeySequence());action.deleteLater()
        self.Shortcuts.clear();self.List.clear()
        for data in self.Store.List():
            item=QListWidgetItem(data['title']+('  ·  '+data['shortcut'] if data.get('shortcut') else ''));item.setData(Qt.ItemDataRole.UserRole,data);self.List.addItem(item)
            if data['id']==identity:self.List.setCurrentItem(item)
            if data.get('shortcut'):
                sequence=QKeySequence(data['shortcut'])
                if any(a.shortcut()==sequence for a in self.Editor.findChildren(QAction) if a not in self.Shortcuts.values()):
                    item.setToolTip(self.Editor.Localization.Translate('macros.shortcut_conflict'));item.setIcon(self.Editor.Resources.Icon('icons/abrowser/warn.svg'));continue
                action=QAction(data['title'],self.Editor);action.setShortcut(sequence);action.triggered.connect(lambda checked=False,d=data:self.Execute(d));self.Editor.addAction(action);self.Shortcuts[data['id']]=action
    def Execute(self,data):
        try:data=self.Store.Load(data['id'])
        except (ValueError,OSError) as error:self.Editor.EditorTools.Error(error,'Macros');return
        owner='macro.'+data['id'];self.Editor.EditorTools.Labels[owner]=data['title'];self.Editor.EditorTools.Call(owner,'OnRun',None,data['modules'][data['entry']],{k:v for k,v in data['modules'].items() if k!=data['entry']})
    def Run(self):
        data=self.Current()
        if data:self.Execute(data)
    def Create(self):self.EditDialog()
    def Edit(self):
        data=self.Current()
        if data:
            try:self.EditDialog(self.Store.Load(data['id']))
            except (ValueError,OSError) as error:self.Editor.EditorTools.Error(error,'Macros')
    def EditDialog(self,data=None):
        e=self.Editor;tr=e.Localization.Translate;dialog=QDialog(e);dialog.setWindowTitle(tr('panel.macros'));form=QFormLayout(dialog);title=QLineEdit(data['title'] if data else '');shortcut=QLineEdit(data.get('shortcut','') if data else '');entry=AssetPickerInput(tr('macros.entry'),accepted_extensions={'.lua'});assets=[a for a in e.Controller._ProjectAssets() if str(a.get('path','')).lower().endswith('.lua')];entry.ConfigureAssets(assets);entry.AssetResolver=e.Runtime.ResolveAssetRecord;entry.PickRequested.connect(entry.OpenProjectPicker);files=QListWidget()
        for record in assets:
            item=QListWidgetItem(str(record.get('name') or Path(record['path']).name));item.setData(Qt.ItemDataRole.UserRole,record);item.setFlags(item.flags()|Qt.ItemFlag.ItemIsUserCheckable);item.setCheckState(Qt.CheckState.Unchecked);files.addItem(item)
        form.addRow(tr('macros.title'),title);form.addRow(tr('macros.shortcut'),shortcut);form.addRow(tr('macros.entry'),entry);form.addRow(tr('macros.modules'),files)
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel);buttons.accepted.connect(dialog.accept);buttons.rejected.connect(dialog.reject);form.addRow(buttons)
        if dialog.exec()!=QDialog.DialogCode.Accepted:return
        try:
            sequence=QKeySequence(shortcut.text())
            owned=self.Shortcuts.get(data['id']) if data else None
            if shortcut.text() and (sequence.isEmpty() or any(a is not owned and a.shortcut()==sequence for a in e.findChildren(QAction))):raise ValueError(tr('macros.shortcut_conflict'))
            record=e.Runtime.ResolveAssetRecord(entry.GetValue()) if entry.GetValue() else None
            records=([record] if record else [])+[files.item(i).data(Qt.ItemDataRole.UserRole) for i in range(files.count()) if files.item(i).checkState()==Qt.CheckState.Checked]
            if record:
                updated=self.Store.Import(title.text(),record['path'],[r['path'] for r in records],shortcut.text(),data['id'] if data else None)
                root=Path(e.Runtime.ProjectInfo()['project_path']).parent;registry=root/'.bazzalt/Editor/MacroSources.json';sources=set()
                if registry.exists():
                    import json
                    sources.update(json.loads(registry.read_text(encoding='utf-8')).get('Assets',[]))
                sources.update(r['uuid'] for r in records);AtomicJson(registry,{'FormatVersion':1,'Assets':sorted(sources)})
            elif data:updated={**data,'title':title.text(),'shortcut':shortcut.text()};self.Store.Save(updated)
            else:raise ValueError(tr('macros.choose_entry'))
            e.EditorTools.Drop('macro.'+updated['id']);self.Refresh(updated['id'])
        except (ValueError,OSError,KeyError) as error:e.EditorTools.Error(error,'Macros')
    def Delete(self):
        data=self.Current()
        if data and QMessageBox.question(self,self.Editor.Localization.Translate('panel.macros'),self.Editor.Localization.Translate('macros.confirm_delete'))==QMessageBox.StandardButton.Yes:self.Editor.EditorTools.Drop('macro.'+data['id']);self.Store.Delete(data['id']);self.Refresh()
    def Menu(self,pos):
        item=self.List.itemAt(pos)
        if item:self.List.setCurrentItem(item)
        menu=EditorMenu("",self.Editor.ThemeManager,self)
        for button,key in self.Buttons:menu.addAction(button.text(),button.click)
        menu.exec(self.List.mapToGlobal(pos))
