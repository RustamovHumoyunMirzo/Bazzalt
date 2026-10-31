"""Extension editor adapter: declarative widgets and scoped commands, no Python plugins."""
from bazzalt.platforms import HostPlatform,HostArchitecture
from pathlib import Path
import json,shutil,sys
from xml.etree import ElementTree as ET
from PySide6.QtCore import QFile,QIODevice,QDir,QTimer,QSignalBlocker
from PySide6.QtGui import QIcon,QAction
from PySide6.QtWidgets import QWidget,QAbstractButton,QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox
from bazzalt.extensions import ExtensionStore,AssetPaths,Local,ExtensionError,AtomicJson
from bazzalt.settings import DataPaths
from Compiler.extensions import EngineVersion,RuntimeIndex,LockProject,LockedItems

WIDGETS={'QWidget','QFrame','QLabel','QPushButton','QToolButton','QCheckBox','QRadioButton','QLineEdit','QPlainTextEdit','QTextEdit','QComboBox','QSpinBox','QDoubleSpinBox','QSlider','QProgressBar','QGroupBox','QScrollArea','QTabWidget','QStackedWidget','QTreeWidget','QListWidget','QTableWidget','QSplitter','QDialogButtonBox'}

def ValidateUI(path):
    data=Path(path).read_bytes()
    if len(data)>1024*1024 or b'<!DOCTYPE' in data or b'<!ENTITY' in data:raise ExtensionError('UI file exceeds limits or contains XML entities')
    root=ET.fromstring(data)
    if root.findall('customwidgets') or root.findall('script'):raise ExtensionError('Custom UI widgets and embedded scripts are forbidden')
    names=set()
    if sum(1 for _ in root.iter('widget'))>512:raise ExtensionError('UI widget count exceeds 512')
    for node in root.iter('widget'):
        if node.get('class') not in WIDGETS:raise ExtensionError('Unsupported extension widget: '+str(node.get('class')))
        name=node.get('name','')
        if name and name in names:raise ExtensionError('Duplicate UI object name: '+name)
        names.add(name)
    # Plugins cannot escape their package through UI resource paths or import
    # arbitrary Qt plugins. Use registered icons and the editor's shared theme.
    if root.findall('.//pixmap') or root.findall('.//iconset') or root.findall('.//resources/include') or root.findall('.//property[@name="styleSheet"]'):raise ExtensionError('Register icons in AssetRegistry; UI resource imports and stylesheets are not allowed')
    return names

class ExtensionController:
    def __init__(self,editor):
        self.Editor=editor;self.Store=ExtensionStore(DataPaths.Root()/'Extensions',EngineVersion(),'macOS' if HostPlatform()=='macos' else HostPlatform().title(),'x86_64' if HostArchitecture()=='x64' else HostArchitecture());self.Items=[];self.Hosts={};self.Panels={};self.Failed=set();self.UndoOwner=None;self.UndoBefore=None;self.RuntimeRoot=DataPaths.Root()/'ExtensionRuntime'/EngineVersion()
        editor.MenuBar.ExtensionsRequested.connect(self.Open);editor.MenuBar.BuildPackageRequested.connect(self.BuildPackage);editor.Runtime.ProjectChanged.connect(self.ProjectLoaded)
        # Run after the base workspace exists, without exposing engine-loop APIs.
        QTimer.singleShot(0,editor,self.Load)
    def Error(self,error):
        from .gui.panels import ConsoleLevel
        self.Editor.Console.AddMessage(str(error),ConsoleLevel.Error,True,'Extensions')
    def Open(self):
        from .gui.extensions import ExtensionsDialog
        ExtensionsDialog(self.Editor).exec()
    def BuildPackage(self):
        from .gui.extensions import PackageBuildDialog
        PackageBuildDialog(self.Editor).exec()
    def Load(self):
        try:
            self.Items=self.Store.Resolve()
            if not self.Items:return
            import hashlib
            fingerprint=hashlib.sha256('|'.join(item['Digest'] for item in self.Items).encode()).hexdigest()
            self.RuntimeRoot=self.RuntimeRoot/fingerprint
            from .runtime import _LoadNativeModule
            self.Native=_LoadNativeModule()
            if self.Native is None or not hasattr(self.Native,'ExtensionScript'):raise ExtensionError('Native extension host is not built')
            RuntimeIndex(self.Items,self.RuntimeRoot/'ext',HostPlatform(),HostArchitecture(),editor=True)
            self.Native.configure_extensions(str(self.RuntimeRoot))
            from PySide6.QtUiTools import QUiLoader
            class Loader(QUiLoader):
                def createWidget(self,classname,parent=None,name=''):
                    if classname not in WIDGETS:raise ExtensionError('Unsupported UI widget')
                    return super().createWidget(classname,parent,name)
            for item in self.Items:
                identity=item['Id'];manifest=item['Manifest'];assets=AssetPaths(manifest)
                for key,panel in manifest['EditorRegistry'].items():
                    path=Local(item['Root'],assets[key]);ValidateUI(path);loader=Loader();loader.setWorkingDirectory(QDir(str(item['Root'])));loader.clearPluginPaths();source=QFile(str(path));source.open(QIODevice.OpenModeFlag.ReadOnly);widget=loader.load(source,self.Editor);source.close()
                    if widget is None:raise ExtensionError(loader.errorString())
                    self.Panels[(identity,key)]=widget
                    icon=QIcon(str(Local(item['Root'],assets[panel['Icon']]))) if panel.get('Icon') else QIcon()
                    dock=self.Editor.Docking.add_panel(widget,panel['Title'],icon,panel_id='extension.'+identity+'.'+key);self.Editor.Docking.close_panel(dock)
                    if panel.get('Script'):
                        for child in widget.findChildren(QWidget):
                            if isinstance(child,QAbstractButton):child.clicked.connect(lambda checked=False,n=child.objectName(),i=identity,s=panel['Script']:self.Call(i,s,'OnClicked',n))
                            elif isinstance(child,(QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox)):
                                signal=child.textEdited if isinstance(child,QLineEdit) else child.currentTextChanged if isinstance(child,QComboBox) else child.valueChanged
                                signal.connect(lambda value,n=child.objectName(),i=identity,s=panel['Script']:self.Call(i,s,'OnChanged',n))
                if manifest['Extends'].get('Scene'):
                    sources={}
                    for name,script in manifest['Scripts'].items():
                        if script['Scope']=='Editor' and Path(assets[script['Id']]).suffix=='.lua':
                            path=Local(item['Root'],assets[script['Id']])
                            if path.stat().st_size>4*1024*1024:raise ExtensionError('Scene tool source exceeds limits')
                            sources[name]=path.read_text(encoding='utf-8-sig')
                    for action in manifest['Extends']['Scene']:
                        try:self.Editor.EditorTools.AddControl('extension.'+identity+'.'+action['Id'],action['Title'],sources[action['Script']],{k:v for k,v in sources.items() if k!=action['Script']},action.get('Function','OnRun'),action.get('Shortcut',''),action.get('Checked',False),action.get('Checkable',True))
                        except (ValueError,OSError) as error:self.Error(str(action['Title'])+': '+str(error))
                for action in manifest['Extends'].get('Menubar',[]):
                    menus={'File':self.Editor.MenuBar.FileMenu,'Edit':self.Editor.MenuBar.EditMenu,'Object':self.Editor.MenuBar.ObjectMenu,'Build':self.Editor.MenuBar.BuildMenu,'View':self.Editor.MenuBar.ViewMenu,'Window':self.Editor.MenuBar.WindowMenu,'Help':self.Editor.MenuBar.HelpMenu}
                    menu=menus.get(action['Menu'])
                    if menu is None:menu=self.Editor.MenuBar.AddMenu(action['Menu'])
                    command=QAction(action['Title'],self.Editor.MenuBar) if menu is self.Editor.MenuBar.WindowMenu else menu.addAction(action['Title'])
                    if menu is self.Editor.MenuBar.WindowMenu:self.Editor.MenuBar._extension_window_actions.append(command)
                    command.triggered.connect(lambda checked=False,i=identity,a=action:self.Call(i,a['Script'],a['Function']))
                for name,script in manifest['Scripts'].items():
                    if script.get('Entry'):self.Call(identity,name,'OnEnable')
            project=self.Editor.Runtime.ProjectInfo().get('project_path')
            if project:self.ProjectLoaded(project)
        except Exception as error:self.Error(error)
    def Command(self,identity,method,target,value):
        controller=self.Editor.Controller
        if method=='GetProject':return str(self.Editor.Runtime.ProjectInfo().get('project_path',''))
        if method=='BeginUndo':
            if self.UndoOwner is not None or controller.History._pending is not None:raise ExtensionError('Finish the current edit before starting an extension edit')
            controller.History.Begin(target or identity);self.UndoOwner=identity;self.UndoBefore=controller.History._pending[1];return ''
        if method=='EndUndo':self.EndUndo(identity);return ''
        if method=='RefreshScene':controller.SetDirty(True);controller.RefreshHierarchy();controller._RefreshInspectorValues();controller._UpdateGizmo();return ''
        if method=='RefreshAssets':self.Editor.Runtime.RefreshAssets();self.Editor.AssetBrowser.Refresh();return ''
        if method=='SelectEntity':controller.SelectEntity(target);return ''
        if method=='Log':self.Editor.Console.AddMessage(target,source=identity);return ''
        if method=='OpenPanel':self.Editor.Docking.open_panel('extension.'+identity+'.'+target);return ''
        panel_name,separator,object_name=target.partition('/')
        name=object_name if separator else target
        candidates=[root if root.objectName()==name else root.findChild(QWidget,name) for (owner,key),root in self.Panels.items() if owner==identity and (not separator or key==panel_name)]
        candidates=[widget for widget in candidates if widget is not None]
        if len(candidates)>1:raise ExtensionError('Ambiguous widget name; use panelId/objectName: '+target)
        widget=candidates[0] if candidates else None
        if widget is None:raise ExtensionError('Extension widget not found: '+target)
        text_getter=getattr(widget,'text',None) or getattr(widget,'toPlainText',None) or getattr(widget,'currentText',None)
        text_setter=getattr(widget,'setText',None) or getattr(widget,'setPlainText',None) or getattr(widget,'setCurrentText',None)
        if method=='GetText' and callable(text_getter):return str(text_getter())
        if method=='GetValue':
            if isinstance(widget,QAbstractButton) and widget.isCheckable():return 'b:'+str(widget.isChecked()).lower()
            if callable(getattr(widget,'value',None)):return 'n:'+str(widget.value())
            if callable(text_getter):return 's:'+str(text_getter())
            raise ExtensionError('Widget does not expose a scalar value: '+target)
        # Programmatic assignments do not recursively invoke user-input handlers.
        blocker=QSignalBlocker(widget)
        if method=='SetText' and callable(text_setter):text_setter(value)
        elif method=='SetValue':
            if isinstance(widget,QAbstractButton) and widget.isCheckable():widget.setChecked(value.lower()=='true')
            elif callable(getattr(widget,'setValue',None)):
                number=float(value)
                if isinstance(widget.value(),int):
                    if not number.is_integer():raise ExtensionError('Integer widget requires an integer')
                    number=int(number)
                widget.setValue(number)
            elif callable(text_setter):text_setter(value)
            else:raise ExtensionError('Widget does not accept a scalar value: '+target)
        elif method=='SetEnabled':widget.setEnabled(value.lower()=='true')
        elif method=='SetVisible':widget.setVisible(value.lower()=='true')
        else:raise ExtensionError('Unsupported editor widget command: '+method)
        del blocker;return ''
    def EndUndo(self,identity,rollback=False):
        if self.UndoOwner is None:return
        if self.UndoOwner!=identity:raise ExtensionError('Another extension owns the current edit')
        controller=self.Editor.Controller;before=self.UndoBefore;self.UndoOwner=None;self.UndoBefore=None
        if rollback:
            controller.History.Cancel();self.Editor.Runtime.RestoreScene(before)
        elif controller.History.Commit():controller.SetDirty(True)
        controller.RefreshHierarchy();controller._RefreshInspectorValues();controller._UpdateGizmo()
    def Call(self,identity,script,function,argument=''):
        key=(identity,script)
        if key in self.Failed:return
        try:
            if self.Editor.Runtime.IsPlaying():
                self.Error(self.Editor.Localization.Translate('extensions.stop_playing'));return
            if Path(self.Native.extension_root())!=self.RuntimeRoot.resolve():self.Native.configure_extensions(str(self.RuntimeRoot))
            if key not in self.Hosts:
                item=next(i for i in self.Items if i['Id']==identity);compiled=item['Manifest']['Scripts'][script].get('Compiled',{}).get('portable')
                if not compiled:raise ExtensionError('Editor controller requires a compiled Lua adapter; native logic is exposed through Bazzalt.require')
                self.Hosts[key]=self.Native.ExtensionScript(str(Local(item['Root'],compiled['Path'])),lambda method,target,value:self.Command(identity,method,target,value),identity)
            self.Hosts[key].call(function,argument)
            if self.UndoOwner==identity:self.EndUndo(identity)
        except Exception as error:
            if self.UndoOwner==identity:self.EndUndo(identity,rollback=True)
            self.Failed.add(key);self.Error(error)
    def ProjectLoaded(self,path):
        if not self.Items:return
        try:
            project=Path(path).parent
            if (project/'Extensions.lock.json').exists():items=LockedItems(project,HostPlatform(),HostArchitecture())
            else:items=self.Items;LockProject(project,items)
            RuntimeIndex(items,project/'.bazzalt/Extensions',HostPlatform(),HostArchitecture())
            from Compiler.extensions import MaterializeAssets
            MaterializeAssets(path,items)
            self.Editor.Runtime.RefreshAssets()
        except (ValueError,OSError) as error:self.Error(error)
    def ComponentIcon(self,name):
        from .scripting import ScriptCompiler
        for item in self.Items:
            manifest=item['Manifest'];assets=AssetPaths(manifest)
            for value in manifest['Extends'].get('Component',{}).values():
                descriptor=ScriptCompiler.Inspect(Local(item['Root'],assets[manifest['Scripts'][value['Script']]['Id']]))
                if descriptor and descriptor.name==name and value.get('Icon'):return QIcon(str(Local(item['Root'],assets[value['Icon']])))
        return None
    def InspectScene(self,scene_id):
        from .gui.widgets import BoolInput,IntInput,FloatInput,StringInput
        host=self.Editor.Runtime._host
        if not host or not hasattr(host,'scene_extension_properties'):return
        values=dict(host.scene_extension_properties(scene_id))
        for item in self.Items:
            identity=item['Id'];fields=item['Manifest']['Extends'].get('SceneSettings',{})
            if not fields:continue
            section=self.Editor.Properties.AddComponentSection('extension.'+identity,item['Manifest']['General']['Name'],removable=False)
            for name,field in fields.items():
                key=identity+':'+name
                try:value=json.loads(values[key]) if key in values else field['Default']
                except (ValueError,TypeError):value=field['Default']
                kind=field['Type']
                import math
                valid=(kind=='bool' and type(value) is bool) or (kind=='int' and type(value) is int and -2147483648<=value<=2147483647) or (kind=='float' and type(value) in (float,int) and math.isfinite(value)) or (kind=='string' and isinstance(value,str))
                if not valid:value=field['Default']
                if kind=='bool':widget=BoolInput(bool(value));signal=widget.ValueChanged
                elif kind=='int':widget=IntInput(value=int(value),minimum=field.get('Min',-2147483648),maximum=field.get('Max',2147483647));signal=widget.valueChanged
                elif kind=='float':widget=FloatInput(value=float(value),minimum=field.get('Min',-1e10),maximum=field.get('Max',1e10));signal=widget.valueChanged
                else:widget=StringInput(str(value));signal=widget.textEdited
                def commit(value,e=identity,n=name):
                    controller=self.Editor.Controller;host=self.Editor.Runtime._host
                    if host is None:return
                    if controller._Mutate('Edit extension scene setting',lambda:host.set_scene_extension_property(scene_id,e,n,json.dumps(value,ensure_ascii=False))):controller.SetDirty(True,[scene_id])
                signal.connect(commit);section.AddField(field.get('Label',name),widget)
    def Shutdown(self):
        for (identity,script) in list(self.Hosts):self.Call(identity,script,'OnDisable')
        self.Hosts.clear()
