"""Personal editor macro bundles. Source snapshots never depend on project files."""
from pathlib import Path
import json,re,uuid
from bazzalt.extensions import AtomicJson
from bazzalt.settings import DataPaths

class MacroStore:
    def __init__(self,root=None):self.Root=Path(root) if root else DataPaths.Root()/'Macros'
    def Load(self,identity):
        if not re.fullmatch('[0-9a-f]{32}',str(identity)):raise ValueError('Invalid macro id')
        path=self.Root/(identity+'.json')
        if path.stat().st_size>32*1024*1024:raise ValueError('Macro bundle exceeds limits')
        data=json.loads(path.read_text(encoding='utf-8'));self.Validate(data)
        if data['id']!=identity:raise ValueError('Macro bundle identity mismatch')
        return data
    def List(self):
        result=[];cache={};updated={}
        if not self.Root.exists():return result
        index=self.Root/'index.json'
        try:
            if index.exists() and index.stat().st_size<=1024*1024:cache=json.loads(index.read_text(encoding='utf-8'))
            if not isinstance(cache,dict):cache={}
        except (OSError,ValueError):cache={}
        for path in sorted(self.Root.glob('*.json'))[:1001]:
            if not re.fullmatch('[0-9a-f]{32}',path.stem):continue
            try:
                stamp=[path.stat().st_mtime_ns,path.stat().st_size];cached=cache.get(path.stem,{})
                if not isinstance(cached,dict):cached={}
                data=cached.get('metadata',{}) if cached.get('stamp')==stamp else {}
                if not isinstance(data,dict) or data.get('id')!=path.stem or not isinstance(data.get('title'),str) or not isinstance(data.get('shortcut',''),str) or not isinstance(data.get('entry'),str) or len(data['title'])>128 or len(data.get('shortcut',''))>128:
                    source=self.Load(path.stem);data={key:source[key] for key in ('version','id','title','entry','shortcut')};data['module_count']=len(source['modules'])
                updated[path.stem]={'stamp':stamp,'metadata':data};result.append(data)
            except (ValueError,OSError,KeyError,TypeError):continue
        if updated!=cache:
            try:AtomicJson(index,updated)
            except OSError:pass
        return sorted(result,key=lambda x:x['title'].casefold())
    @staticmethod
    def Validate(data):
        if not isinstance(data,dict) or type(data.get('version')) is not int or data.get('version')!=1 or not re.fullmatch('[0-9a-f]{32}',str(data.get('id',''))):raise ValueError('Invalid macro bundle')
        if not isinstance(data.get('title'),str) or not data['title'].strip() or len(data['title'])>128:raise ValueError('Macro title is required (128 characters maximum)')
        modules=data.get('modules');entry=data.get('entry')
        if not isinstance(modules,dict) or not 1<=len(modules)<=32 or not isinstance(entry,str) or entry not in modules:raise ValueError('Macro needs one entry and at most 32 Lua scripts')
        total=0
        for name,source in modules.items():
            if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,63}',name) or not isinstance(source,str) or '\0' in source:raise ValueError('Lua module names must be identifiers')
            size=len(source.encode('utf-8'));total+=size
            if size>4*1024*1024 or total>8*1024*1024:raise ValueError('Macro source exceeds limits')
        if not isinstance(data.get('shortcut',''),str) or len(data.get('shortcut',''))>128:raise ValueError('Invalid macro shortcut')
    def Import(self,title,entry,files,shortcut='',identity=None):
        files=list(dict.fromkeys([Path(entry),*(Path(f) for f in files)]));modules={}
        for path in files:
            if path.suffix.lower()!='.lua' or path.stat().st_size>4*1024*1024:raise ValueError('Choose Lua source assets')
            if path.stem.casefold() in {k.casefold() for k in modules}:raise ValueError('Duplicate Lua module names')
            modules[path.stem]=path.read_text(encoding='utf-8-sig')
        data={'version':1,'id':identity or uuid.uuid4().hex,'title':title,'entry':Path(entry).stem,'modules':modules,'shortcut':shortcut};self.Save(data);return data
    def Save(self,data):
        self.Validate(data)
        if not (self.Root/(data['id']+'.json')).exists() and self.Root.exists() and sum(1 for p in self.Root.glob('*.json') if re.fullmatch('[0-9a-f]{32}',p.stem))>=1000:raise ValueError('Macro library limit is 1000 bundles')
        AtomicJson(self.Root/(data['id']+'.json'),data)
    def Delete(self,identity):
        if not re.fullmatch('[0-9a-f]{32}',str(identity)):raise ValueError('Invalid macro id')
        (self.Root/(identity+'.json')).unlink(missing_ok=True)
