"""Versioned extension manifests and bounded, content-addressed .bpack installs.
No Qt, code execution, network access or implicit dependency installation here.
"""
from __future__ import annotations
import hashlib,json,os,re,shutil,stat,tempfile,zipfile
from contextlib import contextmanager
from pathlib import Path,PurePosixPath
import yaml

class ExtensionError(ValueError):pass
ID=re.compile(r"[a-z][a-z0-9_]*(?:[.-][a-z0-9_]+)*\Z")
NAME=re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
MAX_FILE=64*1024*1024
MAX_PACKAGE=512*1024*1024
MAX_FILES=8192
NATIVE_DIRS=("IncludeDirs","LibDirsWin","LibDirsLinux","BinDirsWin","BinDirsLinux","EditorLibDirsWin","EditorLibDirsLinux","EditorBinDirsWin","EditorBinDirsLinux")

def AtomicJson(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,delete=False) as stream:
        temporary=Path(stream.name);json.dump(value,stream,ensure_ascii=False,indent=2);stream.flush();os.fsync(stream.fileno())
    try:os.replace(temporary,path)
    finally:temporary.unlink(missing_ok=True)

def SafeName(name):
    if not isinstance(name,str) or not name or '\\' in name or ':' in name or '\0' in name:raise ExtensionError('Unsafe package path: '+str(name))
    parts=PurePosixPath(name).parts
    if name.startswith('/') or any(p in ('.','..') or p.rstrip(' .')!=p or p.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*(f'COM{i}' for i in range(1,10)),*(f'LPT{i}' for i in range(1,10))} for p in parts):raise ExtensionError('Unsafe package path: '+name)
    if '/'.join(parts)!=name.rstrip('/'):raise ExtensionError('Noncanonical package path: '+name)
    return name

def Local(root,name,*,directory=False):
    SafeName(name);root=Path(root).resolve();path=root/Path(name)
    if any(p.is_symlink() or (hasattr(p,'is_junction') and p.is_junction()) for p in [path,*path.parents] if p!=root and p.is_relative_to(root)):raise ExtensionError('Links are forbidden in extensions: '+name)
    path=path.resolve()
    if not path.is_relative_to(root) or not (path.is_dir() if directory else path.is_file()):raise ExtensionError('Missing extension path: '+name)
    return path

def Version(text):
    match=re.fullmatch(r'v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?',str(text))
    if not match:raise ExtensionError('Invalid semantic version: '+str(text))
    prerelease=match[4]
    tail=tuple((0,int(p)) if p.isdigit() else (1,p) for p in prerelease.split('.')) if prerelease else ()
    return (*map(int,match.group(1,2,3)),prerelease is None,tail)

class UniqueLoader(yaml.SafeLoader):pass
def Mapping(loader,node,deep=False):
    result={}
    for k,v in node.value:
        key=loader.construct_object(k,deep=deep)
        if not isinstance(key,str) or key in result:raise ExtensionError('Manifest keys must be unique strings')
        result[key]=loader.construct_object(v,deep=deep)
    return result
UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,Mapping)

def ReadManifest(path):
    path=Path(path)
    if path.stat().st_size>1024*1024:raise ExtensionError('Manifest exceeds 1 MiB')
    try:value=yaml.load(path.read_text(encoding='utf-8-sig'),Loader=UniqueLoader)
    except (yaml.YAMLError,UnicodeError,RecursionError) as error:raise ExtensionError('Invalid manifest YAML: '+str(error)) from error
    return Validate(value,path.parent)

def Validate(value,root):
    try:return _Validate(value,root)
    except ExtensionError:raise
    except (TypeError,AttributeError,KeyError,RecursionError,OverflowError) as error:raise ExtensionError("Malformed extension manifest structure: "+str(error)) from error

def _Validate(value,root):
    if not isinstance(value,dict):raise ExtensionError('Extension manifest must be a mapping')
    # JSON conversion rejects recursive YAML aliases and non-data values.
    try:value=json.loads(json.dumps(value,allow_nan=False))
    except (TypeError,ValueError,RecursionError) as error:raise ExtensionError('Manifest must contain finite JSON-compatible data') from error
    allowed={'FormatVersion','General','Compatibility','Dependencies','Native','AssetRegistry','EditorRegistry','Scripts','Extends'}
    if set(value)-allowed:raise ExtensionError('Unknown manifest sections: '+', '.join(sorted(set(value)-allowed)))
    if type(value.get('FormatVersion',1)) is not int or value.get('FormatVersion',1)!=1:raise ExtensionError('Unsupported extension format')
    value['FormatVersion']=1
    general=value.get('General');compat=value.get('Compatibility')
    if not isinstance(general,dict) or not isinstance(compat,dict):raise ExtensionError('General and Compatibility are required mappings')
    if len(str(general.get('Id','')))>128 or not ID.fullmatch(str(general.get('Id',''))):raise ExtensionError('General.Id must be a lowercase namespaced identifier')
    SafeName(general['Id'])
    for key in ('Name','ShortDescription','Version'):
        if not isinstance(general.get(key),str) or not general[key].strip():raise ExtensionError('General.'+key+' is required')
    Version(general['Version']);minimum=Version(compat.get('MinEngineVersion',''))
    if compat.get('MaxEngineVersion') and Version(compat['MaxEngineVersion'])<minimum:raise ExtensionError('Maximum engine version precedes minimum')
    for key,allowed_values in (('Platforms',{'Windows','Linux'}),('Architecture',{'x86','x86_64'})):
        if key in compat and (not isinstance(compat[key],list) or not compat[key] or set(compat[key])-allowed_values):raise ExtensionError('Invalid Compatibility.'+key)
    for key in ('HomePageUrl','RepoUrl','DocsUrl'):
        if key in general and (not isinstance(general[key],str) or not general[key].startswith('https://')):raise ExtensionError(key+' must use HTTPS')
    if general.get('LongDescription'):Local(root,general['LongDescription'])
    dependencies=value.setdefault('Dependencies',{}).setdefault('Requires',[])
    if not isinstance(dependencies,list) or len(set(dependencies))!=len(dependencies) or any(not ID.fullmatch(str(p)) or p==general['Id'] for p in dependencies):raise ExtensionError('Invalid extension dependencies')
    assets={};asset_paths=set()
    for asset in value.setdefault('AssetRegistry',[]):
        if not isinstance(asset,dict) or not NAME.fullmatch(str(asset.get('Id',''))) or asset['Id'] in assets:raise ExtensionError('Duplicate or invalid asset Id')
        if Path(asset.get('Path','')).suffix.lower() in {'.py','.pyc','.pyd'}:raise ExtensionError('Python extension code is not supported')
        Local(root,asset.get('Path',''))
        if asset['Path'].casefold() in asset_paths:raise ExtensionError('Duplicate asset path: '+asset['Path'])
        asset_paths.add(asset['Path'].casefold());assets[asset['Id']]=asset['Path']
    scripts=value.setdefault('Scripts',{});modules=set();entries=0
    if not isinstance(scripts,dict):raise ExtensionError('Scripts must be a mapping')
    for key,script in scripts.items():
        if not NAME.fullmatch(key) or not isinstance(script,dict) or script.get('Id') not in assets:raise ExtensionError('Invalid script registration: '+key)
        if Path(assets[script['Id']]).suffix not in {'.lua','.cpp'}:raise ExtensionError('Scripts must reference Lua or C++ source')
        for flag in ('Entry','Public','SelfContained'):
            if flag in script and type(script[flag]) is not bool:raise ExtensionError(flag+' must be boolean')
        module=script.setdefault('ModuleName',key)
        if not NAME.fullmatch(module):raise ExtensionError('Invalid module name: '+module)
        module=module if script.get('Public') and script.get('SelfContained',True) else general['Id']+'.'+module
        if module in modules or module in {'Bazzalt','math','string','table','io','os','package','debug'}:raise ExtensionError('Duplicate/reserved module: '+module)
        modules.add(module)
        if script.get('Entry') and script.get('Public'):raise ExtensionError('Editor entry scripts cannot be public SDK modules')
        if script.get('Entry'):entries+=1
    if entries>1:raise ExtensionError('At most one editor entry script is allowed')
    panels=value.setdefault('EditorRegistry',{})
    for key,panel in panels.items():
        if key not in assets or Path(assets[key]).suffix!='.ui' or not isinstance(panel,dict) or not isinstance(panel.get('Title'),str) or not panel['Title'].strip():raise ExtensionError('EditorRegistry entries require .ui assets and Title')
        if panel.get('Icon') and (panel['Icon'] not in assets or Path(assets[panel['Icon']]).suffix.lower() not in {'.svg','.png','.ico','.jpg','.jpeg','.bmp','.webp'}):raise ExtensionError('Panel Icon must reference a registered image')
        if panel.get('Script') and panel['Script'] not in scripts:raise ExtensionError('Missing panel script')
    editor_scripts={p.get('Script') for p in panels.values() if p.get('Script')}
    editor_scripts.update(a.get('Script') for a in value.get('Extends',{}).get('Menubar',[]))
    for name,script in scripts.items():
        scope=script.setdefault('Scope','Editor' if script.get('Entry') or name in editor_scripts else 'Runtime')
        if scope not in ('Editor','Runtime'):raise ExtensionError('Script Scope must be Editor or Runtime')
        if scope=='Editor' and script.get('Public'):raise ExtensionError('Editor scripts cannot be public gameplay modules')
        if script.get('Entry') and scope!='Editor':raise ExtensionError('Entry scripts are editor-only')
    editor_assets={k for k in panels}
    editor_assets.update(p.get('Icon') for p in panels.values() if p.get('Icon'))
    for asset in value['AssetRegistry']:
        asset.setdefault('Scope','Editor' if asset['Id'] in editor_assets or Path(asset['Path']).suffix=='.ui' else 'Runtime')
        if asset['Scope'] not in ('Editor','Runtime'):raise ExtensionError('Asset Scope must be Editor or Runtime')
    native=value.setdefault('Native',{})
    if type(native.get('Enabled',False)) is not bool:raise ExtensionError('Native.Enabled must be boolean')
    native.setdefault('Scope','Editor' if (panels or editor_scripts or any(s['Scope']=='Editor' for s in scripts.values())) and not any(s['Scope']=='Runtime' for s in scripts.values()) else 'Runtime')
    if native['Scope'] not in ('Editor','Runtime'):raise ExtensionError('Native.Scope must be Editor or Runtime')
    for key in NATIVE_DIRS:
        for item in native.get(key,[]):Local(root,item,directory=True)
    extends=value.setdefault('Extends',{})
    if set(extends)-{'Menubar','Component','SceneSettings'}:raise ExtensionError('Unknown Extends category')
    for item in extends.get('Menubar',[]):
        if not isinstance(item,dict) or not isinstance(item.get('Menu'),str) or not item['Menu'].strip() or not isinstance(item.get('Title'),str) or not item['Title'].strip() or item.get('Script') not in scripts or not NAME.fullmatch(str(item.get('Function',''))):raise ExtensionError('Invalid menu action')
    for name,component in extends.get('Component',{}).items():
        if not NAME.fullmatch(name) or component.get('Script') not in scripts:raise ExtensionError('Invalid component registration')
        if component.get('Icon') and (component['Icon'] not in assets or Path(assets[component['Icon']]).suffix.lower() not in {'.svg','.png','.ico','.jpg','.jpeg','.bmp','.webp'}):raise ExtensionError('Component Icon must reference a registered image')
        script=scripts[component['Script']]
        if script.get('Entry') or script.get('Public') or script['Scope']!='Runtime':raise ExtensionError('Component scripts must be private Runtime behaviors, not entry scripts')
    for name,field in extends.get('SceneSettings',{}).items():
        if not NAME.fullmatch(name) or not isinstance(field,dict) or field.get('Type') not in ('bool','int','float','string'):raise ExtensionError('Invalid scene setting field')
        default=field.get('Default')
        kind=field['Type']
        if (kind=='bool' and type(default) is not bool) or (kind=='int' and type(default) is not int) or (kind=='float' and type(default) not in (int,float)) or (kind=='string' and not isinstance(default,str)):raise ExtensionError('Scene setting default has the wrong type')
        if field.get('Label') is not None and not isinstance(field['Label'],str):raise ExtensionError('Scene setting Label must be text')
        if kind in ('int','float'):
            for bound in ('Min','Max'):
                if bound in field and (type(field[bound]) not in ((int,) if kind=='int' else (int,float))):raise ExtensionError('Invalid numeric scene setting bound')
            if kind=='int' and any(not -2147483648<=v<=2147483647 for v in (default,field.get('Min',default),field.get('Max',default))):raise ExtensionError('Integer scene settings must fit signed 32-bit values')
            if field.get('Min',default)>default or field.get('Max',default)<default:raise ExtensionError('Scene setting default is outside its range')
    return value

def CheckCompatibility(manifest,engine,platform,architecture):
    c=manifest['Compatibility'];current=Version(engine)
    if current<Version(c['MinEngineVersion']) or (c.get('MaxEngineVersion') and current>Version(c['MaxEngineVersion'])):raise ExtensionError('Extension is incompatible with engine '+engine)
    if c.get('Platforms') and platform not in c['Platforms']:raise ExtensionError('Extension does not support '+platform)
    if c.get('Architecture') and architecture not in c['Architecture']:raise ExtensionError('Extension does not support '+architecture)

def AssetPaths(manifest):return {a['Id']:a['Path'] for a in manifest['AssetRegistry']}

def BuildPackage(source,destination,*,progress=lambda text:None,cancel=lambda:False):
    source=Path(source).resolve();root=source.parent;manifest=ReadManifest(source);destination=Path(destination).resolve()
    if destination.suffix.lower()!='.bpack':raise ExtensionError('Output must use .bpack')
    files={};size=0
    def add(path):
        nonlocal size
        name=path.relative_to(root).as_posix();SafeName(name)
        if path.is_symlink() or path.is_junction():raise ExtensionError('Package content cannot contain links')
        n=path.stat().st_size
        if n>MAX_FILE:raise ExtensionError('Package member exceeds limit: '+name)
        if name.casefold() in {n.casefold() for n in files} and name not in files:raise ExtensionError('Case-insensitive filename collision')
        if name in {'extension.json','inventory.json'}:raise ExtensionError('Reserved package filename: '+name)
        if name not in files:size+=n
        files[name]=path
    for asset in manifest['AssetRegistry']:add(Local(root,asset['Path']))
    if manifest['General'].get('LongDescription'):add(Local(root,manifest['General']['LongDescription']))
    for key in NATIVE_DIRS:
        for folder in manifest['Native'].get(key,[]):
            for path in Local(root,folder,directory=True).rglob('*'):
                if path.is_symlink() or path.is_junction():raise ExtensionError('Links are forbidden in extension trees')
                if path.is_file():add(path)
    if size>MAX_PACKAGE or len(files)>MAX_FILES:raise ExtensionError('Package exceeds size or member limit')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent,suffix='.bpack',delete=False) as stream:temporary=Path(stream.name)
    try:
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
            hashes={}
            for name,path in sorted(files.items()):
                if cancel():raise ExtensionError('Package build cancelled')
                progress(name);data=path.read_bytes();hashes[name]=hashlib.sha256(data).hexdigest();archive.writestr(name,data)
            data=json.dumps(manifest,ensure_ascii=False,sort_keys=True).encode('utf-8');archive.writestr('extension.json',data);hashes['extension.json']=hashlib.sha256(data).hexdigest()
            archive.writestr('inventory.json',json.dumps({'FormatVersion':1,'Files':hashes},sort_keys=True))
        if cancel():raise ExtensionError('Package build cancelled')
        os.replace(temporary,destination)
    finally:temporary.unlink(missing_ok=True)
    return destination

def ExtractPackage(package,destination):
    package=Path(package)
    if package.suffix.lower()!='.bpack' or package.stat().st_size>MAX_PACKAGE:raise ExtensionError('Select a bounded .bpack file')
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(package) as archive:
        members=archive.infolist();names=set();total=0
        if len(members)>MAX_FILES+2:raise ExtensionError('Too many package members')
        for item in members:
            SafeName(item.filename)
            if item.is_dir() or stat.S_ISLNK(item.external_attr>>16) or item.flag_bits&1:raise ExtensionError('Links, directory records and encrypted ZIP members are forbidden')
            key=item.filename.casefold()
            if key in names:raise ExtensionError('Duplicate package member')
            names.add(key);total+=item.file_size
            if item.file_size>MAX_FILE or total>MAX_PACKAGE:raise ExtensionError('Unpacked package exceeds limits')
        if any(i.filename in {'extension.json','inventory.json'} and i.file_size>2*1024*1024 for i in members):raise ExtensionError('Package metadata exceeds limit')
        if not {'extension.json','inventory.json'}<=names:raise ExtensionError('Missing package manifest or inventory')
        inventory=json.loads(archive.read('inventory.json'));hashes=inventory.get('Files',{})
        if inventory.get('FormatVersion')!=1 or set(hashes)!={i.filename for i in members if i.filename!='inventory.json'}:raise ExtensionError('Inventory does not match package members')
        for item in members:
            if item.filename=='inventory.json':continue
            data=archive.read(item)
            if hashlib.sha256(data).hexdigest()!=hashes[item.filename]:raise ExtensionError('Package integrity failure: '+item.filename)
            target=destination/item.filename;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        (destination/'inventory.json').write_bytes(archive.read('inventory.json'))
        manifest=json.loads((destination/'extension.json').read_text(encoding='utf-8'));return Validate(manifest,destination)

def VerifyInstalled(root):
    root=Path(root);path=Local(root,'inventory.json')
    if path.stat().st_size>2*1024*1024:raise ExtensionError('Installed extension inventory exceeds limit')
    value=json.loads(path.read_text(encoding='utf-8'))
    if value.get('FormatVersion')!=1 or not isinstance(value.get('Files'),dict) or len(value['Files'])>MAX_FILES+1:raise ExtensionError('Invalid installed extension inventory')
    for name,digest in value['Files'].items():
        source=Local(root,name)
        if source.stat().st_size>MAX_FILE or hashlib.sha256(source.read_bytes()).hexdigest()!=digest:raise ExtensionError('Installed extension integrity failure: '+name)


class ExtensionStore:
    def __init__(self,root,engine,platform,architecture='x86_64'):
        self.Root=Path(root).resolve();self.Engine=engine;self.Platform=platform;self.Architecture=architecture;self.State=self.Root/'state.json'
    def _state(self):
        if not self.State.exists():return {'FormatVersion':1,'Installed':{}}
        if self.State.stat().st_size>1024*1024:raise ExtensionError('Extension state exceeds limit')
        value=json.loads(self.State.read_text(encoding='utf-8'))
        if value.get('FormatVersion')!=1:raise ExtensionError('Unsupported extension state version')
        return value
    def Installed(self):
        result=[]
        for identity,item in self._state()['Installed'].items():
            if not ID.fullmatch(identity) or not re.fullmatch('[0-9a-f]{64}',item['Digest']):raise ExtensionError('Invalid installation state')
            root=self.Root/'packages'/item['Digest'];manifest=Validate(json.loads((root/'extension.json').read_text(encoding='utf-8')),root)
            result.append({'Id':identity,'Enabled':bool(item.get('Enabled',False)),'Root':root,'Manifest':manifest,'Digest':item['Digest']})
        return result
    @contextmanager
    def _lock(self):
        self.Root.mkdir(parents=True,exist_ok=True)
        with (self.Root/'store.lock').open('a+b') as file:
            file.seek(0);file.write(b'\0');file.flush();file.seek(0)
            try:
                if os.name=='nt':
                    import msvcrt
                    msvcrt.locking(file.fileno(),msvcrt.LK_NBLCK,1)
                else:
                    import fcntl
                    fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except OSError as error:raise ExtensionError('Another editor is updating extensions') from error
            try:yield
            finally:
                if os.name=='nt':file.seek(0);msvcrt.locking(file.fileno(),msvcrt.LK_UNLCK,1)
                else:fcntl.flock(file,fcntl.LOCK_UN)
    def Import(self,package):
        with self._lock():return self._Import(package)
    def _Import(self,package):
        self.Root.mkdir(parents=True,exist_ok=True)
        if Path(package).stat().st_size>MAX_PACKAGE:raise ExtensionError('Package exceeds size limit')
        digest=hashlib.sha256(Path(package).read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix='install-',dir=self.Root) as temporary:
            manifest=ExtractPackage(package,temporary);CheckCompatibility(manifest,self.Engine,self.Platform,self.Architecture);state=self._state();identity=manifest['General']['Id']
            existing=state['Installed'].get(identity)
            if existing and existing.get('Enabled'):raise ExtensionError('Disable an extension before replacing it')
            final=self.Root/'packages'/digest;final.parent.mkdir(exist_ok=True)
            if not final.exists():os.replace(temporary,final)
            state['Installed'][identity]={'Digest':digest,'Enabled':False};AtomicJson(self.State,state)
        return identity
    def Resolve(self,enabled=None,installed=None):
        installed={item['Id']:item for item in (self.Installed() if installed is None else installed)};enabled=set(enabled if enabled is not None else (k for k,v in installed.items() if v['Enabled']))
        ordered=[];visiting=set();visited=set();modules={};components={}
        def visit(identity):
            if identity in visiting:raise ExtensionError('Extension dependency cycle at '+identity)
            if identity in visited:return
            if identity not in installed or identity not in enabled:raise ExtensionError('Required extension is not enabled: '+identity)
            item=installed[identity];VerifyInstalled(item['Root']);CheckCompatibility(item['Manifest'],self.Engine,self.Platform,self.Architecture);visiting.add(identity)
            for dependency in item['Manifest']['Dependencies']['Requires']:visit(dependency)
            for script in item['Manifest']['Scripts'].values():
                if script.get('Public'):
                    name=script['ModuleName'] if script.get('SelfContained',True) else identity+'.'+script['ModuleName']
                    if name in modules:raise ExtensionError('Public module collision: '+name)
                    modules[name]=identity
            from Editor.scripting import ScriptCompiler
            assets=AssetPaths(item['Manifest'])
            for name,component in item['Manifest']['Extends'].get('Component',{}).items():
                source=assets[item['Manifest']['Scripts'][component['Script']]['Id']];descriptor=ScriptCompiler.Inspect(Local(item['Root'],source))
                if not descriptor or descriptor.is_subsystem:raise ExtensionError('Extension component must declare a behavior')
                if descriptor.name in components:raise ExtensionError('Extension component name collision: '+descriptor.name)
                components[descriptor.name]=identity
            visiting.remove(identity);visited.add(identity);ordered.append(item)
        for identity in sorted(enabled):visit(identity)
        return ordered
    def SetEnabled(self,identity,enabled):
        with self._lock():return self._SetEnabled(identity,enabled)
    def _SetEnabled(self,identity,enabled):
        state=self._state()
        if identity not in state['Installed']:raise ExtensionError('Extension is not installed')
        selected={k for k,v in state['Installed'].items() if v.get('Enabled')}
        if enabled:selected.add(identity)
        else:selected.discard(identity)
        if enabled:self.Resolve(selected)
        else:
            installed={i['Id']:i for i in self.Installed()}
            for name in selected:
                if identity in installed[name]['Manifest']['Dependencies']['Requires']:raise ExtensionError('Enabled extension depends on '+identity+': '+name)
        state['Installed'][identity]['Enabled']=bool(enabled);AtomicJson(self.State,state)
