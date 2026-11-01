"""Prefab asset authoring. Uses the native serializer; does not copy inspector fields."""
from pathlib import Path
from PySide6.QtWidgets import QFileDialog,QLabel,QPushButton,QWidget,QHBoxLayout
class PrefabEditor:
    def __init__(self,controller):
        self.Controller=controller
        self.Indicator=None
        controller.History.Changed.connect(self.RefreshIndicator)
        controller.Window.Hierarchy.ContextMenuRequested.connect(self.HierarchyMenu)
        controller.Window.AssetBrowser.ContextMenuRequested.connect(self.AssetMenu)
        controller.Window.AssetBrowser.EntityDropped.connect(self.CreateAt)
    def _Selected(self):
        values=self.Controller.SelectedEntities
        return values[0] if len(values)==1 else ""
    def HierarchyMenu(self,menu,_position):
        c=self.Controller;entity=self._Selected()
        if not entity:return
        tr=c.Window.Localization.Translate;menu.addSeparator();group=menu.addMenu(tr("prefab.menu"));group.setEnabled(bool(c.Runtime.AssetDirectory()) and not c.Runtime.IsPlaying() and not c.Runtime.IsEditorLocked(entity))
        group.addAction(tr("prefab.create")).triggered.connect(self.Create)
        info=c.Runtime.PrefabInfo(entity)
        if not info:return
        group.addSeparator()
        for operation in ("apply","revert","refresh","unpack"):
            action=group.addAction(tr("prefab."+operation));action.setEnabled(operation=="unpack" or bool(info.get("available")));action.triggered.connect(lambda _=False,op=operation:self.Operation(op))
    def AssetMenu(self,menu,_position):
        c=self.Controller;tr=c.Window.Localization.Translate
        if self._Selected():menu.addAction(tr("prefab.create")).triggered.connect(self.Create)
    def Create(self):
        c=self.Controller;entity=self._Selected()
        if not entity or c.Runtime.IsPlaying():return
        tr=c.Window.Localization.Translate;name=str(c.Runtime.EntityDetails(entity).get("name","Prefab"));name="".join(char for char in name if char.isalnum() or char in " _-").strip() or "Prefab"
        path,_=QFileDialog.getSaveFileName(c.Window,tr("prefab.create"),str(c.Window.AssetBrowser.CurrentFolder()/(name+".bprefab")),tr("prefab.file_filter"))
        if not path:return
        path=Path(path)
        if path.suffix.lower()!=".bprefab":path=path.with_name(path.name+".bprefab")
        c.History.BeginScenes(tr("prefab.create"))
        c.History.RecordFile(path);c.History.RecordFile(path.with_name(path.name+".meta"))
        if not self.PrepareScripts(entity):c.History.Cancel();return
        if c.Runtime.SavePrefab(entity,path):c.History.Commit();c.SetDirty(True);c.RefreshHierarchy();c.Window.AssetBrowser.Refresh(path);c.SelectEntity(entity,force=True)
        else:c.History.Cancel()
    def Instantiate(self,path,parent):
        c=self.Controller;c.History.BeginScenes(c.Window.Localization.Translate("prefab.instantiate"))
        if c.ScriptAttachments:c.History.RecordFile(c.ScriptAttachments.path)
        entity=c.Runtime.InstantiatePrefabPath(path,parent)
        if entity:self.MergeScripts();c.History.Commit();c.SetDirty(True,[c._SceneForParent(parent)]);c.RefreshHierarchy();c.SelectEntity(entity)
        else:c.History.Cancel()
    def Operation(self,operation):
        c=self.Controller;entity=self._Selected()
        if not entity:return
        c.History.BeginScenes(c.Window.Localization.Translate("prefab."+operation))
        if operation=="apply":
            path=Path(c.Runtime.PrefabInfo(entity)["path"])
            try:c.History.RecordFile(path);c.History.RecordFile(path.with_name(path.name+".meta"))
            except (OSError,ValueError,RuntimeError) as error:c.History.Cancel();c.Runtime.ErrorOccurred.emit(str(error));return
        if operation=="apply" and not self.PrepareScripts(entity):c.History.Cancel();return
        if c.ScriptAttachments:c.History.RecordFile(c.ScriptAttachments.path)
        result=c.Runtime.PrefabOperation(entity,operation)
        if result:
            self.MergeScripts()
            c.History.Commit()
            # Applying an asset can update instances in any loaded scene.
            c.SetDirty(True,[scene["uuid"] for scene in c.Runtime.LoadedScenes()]);c.RefreshHierarchy();c.SelectEntity(result,force=True)
        else:c.History.Cancel()

    def Inspect(self,entity):
        c=self.Controller;info=getattr(c.Runtime,'PrefabInfo',lambda _:{})(entity)
        if not info:return
        tr=c.Window.Localization.Translate;section=c.Window.Properties.AddComponentSection('prefab',tr('prefab.menu'),removable=False)
        section.AddField(tr('prefab.source'),QLabel(Path(info['path']).stem if info.get('available') else tr('prefab.missing')))
        indicator=QLabel(tr('common.yes' if info.get('overrides') else 'common.no'));self.Indicator=(entity,indicator)
        section.AddField(tr('prefab.overrides'),indicator)
        controls=QWidget();layout=QHBoxLayout(controls);layout.setContentsMargins(0,0,0,0);layout.setSpacing(4)
        for operation in ('apply','revert','refresh','unpack'):
            button=QPushButton(tr('prefab.short.'+operation));button.setToolTip(tr('prefab.'+operation));button.setEnabled(not c.Runtime.IsPlaying() and (operation=='unpack' or info.get('available',False)));button.clicked.connect(lambda _=False,op=operation:self.Operation(op));layout.addWidget(button)
        section.AddWideWidget(controls)
    def InspectAsset(self,path):
        c=self.Controller;tr=c.Window.Localization.Translate;section=c.Window.Properties.AddComponentSection('asset.prefab',tr('prefab.menu'),removable=False)
        button=QPushButton(tr('prefab.instantiate'));button.clicked.connect(lambda:self.Instantiate(path,''));section.AddWideWidget(button)

    def CreateAt(self,entity,folder):
        c=self.Controller
        if c.Runtime.IsPlaying() or c.Runtime.IsEditorLocked(entity) or not folder:return
        root=Path(c.Runtime.AssetDirectory()).resolve();folder=Path(folder).resolve()
        if not folder.is_relative_to(root):return
        name=str(c.Runtime.EntityDetails(entity).get('name','Prefab'));name=''.join(char for char in name if char.isalnum() or char in ' _-').strip() or 'Prefab'
        path=c.Window.AssetBrowser._Unique(name+'.bprefab',folder);c.History.BeginScenes(c.Window.Localization.Translate('prefab.create'))
        c.History.RecordFile(path);c.History.RecordFile(path.with_name(path.name+'.meta'))
        if not self.PrepareScripts(entity):c.History.Cancel();return
        if c.Runtime.SavePrefab(entity,path):c.History.Commit();c.SetDirty(True);c.RefreshHierarchy();c.Window.AssetBrowser.Refresh(path)
        else:c.History.Cancel()

    def RefreshIndicator(self):
        from shiboken6 import isValid
        if not self.Indicator:return
        entity,label=self.Indicator
        if not isValid(label):self.Indicator=None;return
        info=self.Controller.Runtime.PrefabInfo(entity)
        label.setText(self.Controller.Window.Localization.Translate('common.yes' if info.get('overrides') else 'common.no'))

    def PrepareScripts(self,root):
        c=self.Controller
        if not c.ScriptAttachments or not c.ScriptCompiler:return True
        entities={str(row['uuid']):row for scene in c.Runtime.LoadedScenes() for row in scene.get('entities',())}
        ids={root};changed=True
        while changed:
            previous=len(ids);ids.update(identity for identity,row in entities.items() if str(row.get('parent','')) in ids);changed=len(ids)!=previous
        records=[]
        try:
            for identity in ids:
                for attachment in c.ScriptAttachments.For(identity):
                    descriptor=c.ScriptCompiler.Inspect(attachment['source'])
                    if descriptor is None:continue
                    properties=[]
                    for prop in descriptor.properties:
                        value=attachment.get('properties',{}).get(prop.name,'')
                        text=('true' if value else 'false') if isinstance(value,bool) else ','.join(str(part) for part in value) if isinstance(value,(list,tuple)) else str(value)
                        properties.append(dict(name=prop.name,type=prop.type,value=text))
                    records.append(dict(entity=identity,source=attachment['source'],type=attachment['type'],enabled=attachment.get('enabled',True),properties=properties))
            if records and not c.Runtime.SyncPrefabScripts(records):raise ValueError(c.Runtime.LastError())
            return True
        except (OSError,ValueError,RuntimeError) as error:c.Runtime.ErrorOccurred.emit(str(error));return False
    def MergeScripts(self):
        c=self.Controller
        if not c.ScriptAttachments:return
        for row in c.Runtime.SceneScripts():
            entries=c.ScriptAttachments.values.setdefault('entities',{}).setdefault(row['entity'],[])
            record={key:row[key] for key in ('source','type','enabled','properties')}
            previous=next((entry for entry in entries if entry.get('type')==record['type']),None)
            if previous:previous.update(record)
            else:entries.append(record)
        c.ScriptAttachments.Save()
    def NativeSources(self):
        import yaml
        c=self.Controller;directory=c.Runtime.AssetDirectory()
        if not directory:return []
        root=Path(directory)
        if not root.is_dir():return []
        records={str(row['uuid']):Path(row['path']) for row in c._ProjectAssets()};sources=[]
        for path in root.rglob('*.bprefab'):
            if path.stat().st_size>16*1024*1024:continue
            try:document=yaml.safe_load(path.read_text(encoding='utf-8')) or {}
            except (OSError,ValueError,yaml.YAMLError):continue
            for entity in document.get('Entities') or []:
                for component in entity.get('Components') or []:
                    if component.get('Type')!='Bazzalt.ScriptComponents':continue
                    properties=component.get('Properties') or {}
                    for key,value in properties.items():
                        if key.endswith('.Source'):
                            source=records.get(str(value),Path(str(value)));source=source if source.is_absolute() else Path(c.Runtime.ProjectInfo()['project_path']).parent/source
                            if source.suffix.lower()=='.cpp' and str(source) not in sources:sources.append(str(source))
        return sources
    def RegisterModules(self,sources,outputs):
        modules={}
        for source,output in zip(sources,outputs):
            descriptor=self.Controller.ScriptCompiler.Inspect(source)
            if descriptor:modules[descriptor.name]=str(output)
        self.Controller.Runtime.SetPrefabScriptModules(modules)
