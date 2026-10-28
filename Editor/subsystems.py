"""Project-owned C++ subsystem registrations, shared by editor and game compiler."""
import copy,json
from pathlib import Path
import yaml

KEY='application.subsystems'
LEVELS=('boot','window','initialization','content','ready','before_input','input','after_input','before_audio','audio','after_audio','before_physics','physics','after_physics')
LEVEL_ORDER=(0,1,2,5,6,7,8,9,10,11,12,13,3,4)

def Registrations(properties):
    raw=properties.get(KEY,'[]') or '[]'
    if not isinstance(raw,str) or len(raw)>65536:raise ValueError('Invalid subsystem registration data')
    values=json.loads(raw)
    if not isinstance(values,list) or len(values)>128:raise ValueError('At most 128 global subsystems may be registered')
    seen=set();result=[]
    for value in values:
        if not isinstance(value,dict) or set(value)!={'asset','level'}:raise ValueError('Invalid subsystem registration')
        import uuid
        identity=str(uuid.UUID(str(value['asset'])))
        if identity=='00000000-0000-0000-0000-000000000000' or identity in seen:raise ValueError('Duplicate or empty subsystem UUID')
        level=value['level']
        if isinstance(level,bool) or not isinstance(level,int) or not 0<=level<len(LEVELS):raise ValueError('Invalid subsystem load level')
        seen.add(identity);result.append(dict(asset=identity,level=level))
    return result

def Sources(project,registrations):
    if not registrations:return {}
    project=Path(project);metadata=yaml.safe_load(project.read_text(encoding='utf-8-sig')) or {}
    assets=(project.parent/metadata.get('AssetDirectory','Assets')).resolve()
    if not assets.is_relative_to(project.parent.resolve()) or assets==project.parent.resolve():raise ValueError('Invalid project assets directory')
    by_id={}
    for path in assets.rglob('*.meta'):
        if path.stat().st_size>4*1024*1024:raise ValueError('Asset metadata exceeds size limit')
        item=yaml.safe_load(path.read_text(encoding='utf-8-sig')) or {}
        if isinstance(item,dict) and item.get('UUID'):by_id[str(item['UUID']).lower()]=Path(str(path)[:-5]).resolve()
    selected={};names=set()
    for item in registrations:
        path=by_id.get(item['asset'])
        if path is None or not path.is_file() or path.suffix.lower()!='.cpp':raise ValueError('Subsystem must reference an existing C++ asset: '+item['asset'])
        if path.is_symlink() or not path.is_relative_to(assets):raise ValueError('Subsystem source escapes project assets')
        from .scripting import ScriptCompiler
        descriptor=ScriptCompiler.Inspect(path)
        if descriptor is None:raise ValueError('Declare SUBSYSTEM(Name) in '+path.name)
        if descriptor.name in names:raise ValueError('Duplicate subsystem type: '+descriptor.name)
        names.add(descriptor.name)
        selected[item['asset']]=path
    def Check(source):
        text=str(source)
        path=by_id.get(text.lower())
        if path is None:
            path=Path(text);path=(path if path.is_absolute() else project.parent/path).resolve()
        if path in selected.values():raise ValueError('Subsystem script is attached to an entity: '+path.name)
    legacy=project.parent/'.bazzalt/ScriptAttachments.json'
    if legacy.is_file():
        for entity,entries in (json.loads(legacy.read_text(encoding='utf-8')).get('entities') or {}).items():
            if entity.startswith('@'):continue
            for entry in entries:Check(entry.get('source',''))
    for scene in assets.rglob('*.bscene'):
        if scene.is_symlink() or not scene.resolve().is_relative_to(assets) or scene.stat().st_size>64*1024*1024:raise ValueError('Unsafe or oversized scene in subsystem validation')
        data=yaml.safe_load(scene.read_text(encoding='utf-8-sig')) or {}
        if not isinstance(data,dict) or not isinstance(data.get('Entities',[]),(list,type(None))):raise ValueError('Invalid scene in subsystem validation')
        for entity in data.get('Entities') or []:
            if not isinstance(entity,dict):raise ValueError('Invalid scene entity')
            for component in entity.get('Components') or []:
                if not isinstance(component,dict):raise ValueError('Invalid scene component')
                if component.get('Type')!='Bazzalt.ScriptComponents':continue
                fields=component.get('Properties') or {};count=int(fields.get('Count',0))
                if not 0<=count<=1024:raise ValueError('Invalid script attachment count')
                for i in range(count):Check(fields.get(f'Script.{i}.Source',''))
    return selected

def WithSubsystems(attachments,sources):
    from .scripting import ScriptCompiler,ScriptValidationError
    result=copy.copy(attachments);result.values=copy.deepcopy(attachments.values)
    entities=result.values.setdefault('entities',{})
    for key in list(entities):
        if key.startswith('@subsystem:'):entities.pop(key)
    for identity,path in sources.items():
        descriptor=ScriptCompiler.Inspect(path)
        if descriptor is None:raise ScriptValidationError('Declare SUBSYSTEM(Name) in '+path.name)
        entities['@subsystem:'+identity]=[dict(source=str(path),type=descriptor.name,enabled=True,properties={})]
    return result
