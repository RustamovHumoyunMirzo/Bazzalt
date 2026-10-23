"""Versioned project build profiles; secrets never belong in this document."""
import copy
import json
from pathlib import Path
import re

DEFAULTS={
    'version':1,'production':{'name':'Game','company':'','version':'1.0.0','copyright':'','license':'','description':''},
    'target':{'architecture':'x64','startup_scene':'','runtime_directory':''},
    'window':{'title':'Game','width':1280,'height':720,'resizable':True,'mode':'windowed','backend':'automatic'},
    'intro':{'enabled':True,'duration_ms':2000,'logo_size':144,'allow_skip':False},
    'packaging':{'format':'pak','archive':False,'chunk_mb':64,'decoy_kb':0},
    'security':{'preset':'low','encryption':'none','key_storage':'embedded','key_environment':'BAZZALT_GAME_KEY'},
}
PRESETS={
    'low':{'encryption':'none','key_storage':'embedded','chunk_mb':64,'decoy_kb':0},
    'medium':{'encryption':'aes-256-gcm','key_storage':'embedded','chunk_mb':64,'decoy_kb':0},
    'high':{'encryption':'aes-256-gcm','key_storage':'environment','chunk_mb':32,'decoy_kb':0},
    'very_high':{'encryption':'aes-256-gcm','key_storage':'environment','chunk_mb':16,'decoy_kb':0},
}
def PathFor(project):return Path(project).parent/'BuildSettings.json'
def Merge(value):
    result=copy.deepcopy(DEFAULTS)
    if not isinstance(value,dict) or value.get('version',1)!=1:raise ValueError('Unsupported build settings version')
    for section,fields in value.items():
        if section in result and isinstance(fields,dict):result[section].update({k:v for k,v in fields.items() if k in result[section]})
    return result
def Load(project):
    path=PathFor(project)
    return Merge(json.loads(path.read_text(encoding='utf-8-sig'))) if path.exists() else Merge({})
def Validate(value):
    value=Merge(value);name=value['production']['name']
    for section,fields in DEFAULTS.items():
        if isinstance(fields,dict):
            for key,default in fields.items():
                if type(value[section][key]) is not type(default):raise ValueError(f'Invalid type for {section}.{key}')
    if not isinstance(name,str) or not re.fullmatch(r'[\w -]{1,80}',name) or name.endswith((' ','.')) or name.upper() in {'CON','PRN','AUX','NUL',*[f'COM{i}' for i in range(1,10)],*[f'LPT{i}' for i in range(1,10)]}:raise ValueError('Invalid executable name')
    for key,field in value['production'].items():
        if not isinstance(field,str) or '\0' in field or len(field)>65535:raise ValueError('Invalid production metadata: '+key)
    version=value['production']['version']
    if not re.fullmatch(r'\d+(?:\.\d+){1,3}',version) or any(int(part)>65535 for part in version.split('.')):raise ValueError('Product version must contain 2 to 4 numeric parts between 0 and 65535')
    choices={'target':{'architecture':('x64','x86')},'window':{'mode':('windowed','maximized','borderless','fullscreen'),'backend':('automatic','vulkan','opengl')},'packaging':{'format':('loose','pak')},'security':{'preset':tuple(PRESETS)+('custom',),'encryption':('none','aes-256-gcm'),'key_storage':('embedded','file','environment')}}
    for section,fields in choices.items():
        for key,items in fields.items():
            if value[section][key] not in items:raise ValueError(f'Invalid {section}.{key}')
    for section,key,low,high in (('window','width',320,16384),('window','height',200,16384),('packaging','chunk_mb',4,256),('packaging','decoy_kb',0,1024),('intro','duration_ms',0,30000),('intro','logo_size',48,512)):
        field=value[section][key]
        if type(field)!=int or not low<=field<=high:raise ValueError(f'Invalid {section}.{key}')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,100}',value['security']['key_environment']):raise ValueError('Invalid key environment variable')
    if value['security']['encryption']!='none' and value['packaging']['format']!='pak':raise ValueError('Encryption requires BPAK packaging')
    return value
def Save(project,value):
    value=Validate(value);path=PathFor(project);temporary=path.with_suffix('.json.tmp');temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(path)
def ApplyPreset(value,preset):
    value=copy.deepcopy(value);settings=PRESETS[preset];value['security'].update({k:v for k,v in settings.items() if k in value['security']});value['security']['preset']=preset
    value['packaging'].update({k:v for k,v in settings.items() if k in value['packaging']})
    if settings['encryption']!='none':value['packaging']['format']='pak'
    return value
