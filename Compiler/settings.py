"""Versioned project build profiles; secrets never belong in this document."""
import copy
import json
from pathlib import Path
import re

DEFAULTS={
    'version':2,'production':{'name':'Game','company':'','version':'1.0.0','copyright':'','license':'','description':''},
    'target':{'platform':'windows','architecture':'x64','startup_scene':'','runtime_directory':''},
    'window':{'title':'Game','width':1280,'height':720,'resizable':True,'mode':'windowed','backend':'automatic'},
    'intro':{'enabled':True,'duration_ms':2000,'logo_size':144,'allow_skip':False},
    'packaging':{'format':'pak','archive':False,'chunk_mb':64,'decoy_kb':0,'include_assets':'**','exclude_assets':''},
    'security':{'preset':'low','encryption':'none','key_storage':'embedded','key_environment':'BAZZALT_GAME_KEY','encrypt_manifests':False},
}
PLATFORM_DEFAULTS={
    'windows':{'architecture':'x64','runtime_directory':'','backend':'automatic','key_storage':'embedded'},
    'linux':{'architecture':'x64','runtime_directory':'','backend':'automatic','key_storage':'embedded'},
    'macos':{'architecture':'arm64','runtime_directory':'','backend':'automatic','key_storage':'embedded','bundle_identifier':'com.example.game','minimum_version':'12.0','signing_identity':'-','notarization_profile':''},
}

def Effective(value):
    result=copy.deepcopy(value);selected=result['target']['platform'];profile=result.get('platforms',{}).get(selected)
    if profile:
        for key in ('architecture','runtime_directory'):result['target'][key]=profile[key]
        result['window']['backend']=profile['backend'];result['security']['key_storage']=profile['key_storage']
    return result

PRESETS={
    'low':{'encryption':'none','key_storage':'embedded','encrypt_manifests':False,'chunk_mb':64,'decoy_kb':0},
    'medium':{'encryption':'aes-256-gcm','key_storage':'embedded','encrypt_manifests':True,'chunk_mb':64,'decoy_kb':0},
    'high':{'encryption':'aes-256-gcm','key_storage':'environment','encrypt_manifests':True,'chunk_mb':32,'decoy_kb':0},
    'very_high':{'encryption':'aes-256-gcm','key_storage':'environment','encrypt_manifests':True,'chunk_mb':16,'decoy_kb':0},
}
def PathFor(project):return Path(project).parent/'BuildSettings.json'
def Merge(value):
    result=copy.deepcopy(DEFAULTS)
    if not isinstance(value,dict) or type(value.get('version',1)) is not int or value.get('version',1) not in (1,2):raise ValueError('Unsupported build settings version')
    for section,fields in value.items():
        if section in result and isinstance(result[section],dict) and not isinstance(fields,dict):raise ValueError('Invalid build settings section: '+section)
        if section=='security' and any(key not in result['security'] for key in fields):raise ValueError('Unsupported security setting; update the compiler or correct the profile')
        if section in result and isinstance(fields,dict):result[section].update({k:v for k,v in fields.items() if k in result[section]})
    if result['security']['encryption']!='none' and 'encrypt_manifests' not in value.get('security',{}):result['security']['encrypt_manifests']=True
    result['platforms']=copy.deepcopy(PLATFORM_DEFAULTS)
    if 'platforms' not in value:
        selected=result['target']['platform']
        if selected in result['platforms']:
            profile=result['platforms'][selected]
            for key in ('architecture','runtime_directory'):profile[key]=result['target'][key]
            profile['backend']=result['window']['backend'];profile['key_storage']=result['security']['key_storage']
    else:
        profiles=value['platforms']
        if not isinstance(profiles,dict) or set(profiles)-set(PLATFORM_DEFAULTS):raise ValueError('Invalid platform profiles')
        for name,fields in profiles.items():
            if not isinstance(fields,dict) or set(fields)-set(PLATFORM_DEFAULTS[name]):raise ValueError('Invalid platform settings: '+name)
            result['platforms'][name].update(fields)
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
    choices={'target':{'platform':('windows','linux','macos'),'architecture':('x64','x86','arm64')},'window':{'mode':('windowed','maximized','borderless','fullscreen'),'backend':('automatic','vulkan','opengl','metal')},'packaging':{'format':('loose','pak')},'security':{'preset':tuple(PRESETS)+('custom',),'encryption':('none','aes-256-gcm'),'key_storage':('embedded','file','environment','dpapi_user','keychain')}}
    for name,profile in value['platforms'].items():
        for key,default in PLATFORM_DEFAULTS[name].items():
            if type(profile[key]) is not type(default) or isinstance(profile[key],str) and ('\0' in profile[key] or len(profile[key])>4096):raise ValueError('Invalid platform field: '+name+'.'+key)
        if profile['architecture'] not in (('x64','arm64') if name=='macos' else ('x64','x86')):raise ValueError('Unsupported '+name+' architecture')
        if profile['backend'] not in (('automatic','metal') if name=='macos' else ('automatic','vulkan','opengl')):raise ValueError('Unsupported '+name+' backend')
        if profile['key_storage'] not in ('embedded','file','environment',*(['dpapi_user'] if name=='windows' else ['keychain'] if name=='macos' else [])):raise ValueError('Unsupported '+name+' key storage')
    mac=value['platforms']['macos']
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9-]+)+',mac['bundle_identifier']):raise ValueError('Invalid macOS bundle identifier')
    if not re.fullmatch(r'\d{2}\.\d+(?:\.\d+)?',mac['minimum_version']) or int(mac['minimum_version'].split('.')[0])<12:raise ValueError('macOS deployment target must be 12.0 or later')
    if not mac['signing_identity'].strip() or mac['signing_identity'].startswith('--'):raise ValueError('Specify a macOS signing identity or - for ad-hoc signing')
    for section,fields in choices.items():
        for key,items in fields.items():
            if value[section][key] not in items:raise ValueError(f'Invalid {section}.{key}')
    for section,key,low,high in (('window','width',320,16384),('window','height',200,16384),('packaging','chunk_mb',4,256),('packaging','decoy_kb',0,1024),('intro','duration_ms',0,30000),('intro','logo_size',48,512)):
        field=value[section][key]
        if type(field)!=int or not low<=field<=high:raise ValueError(f'Invalid {section}.{key}')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,100}',value['security']['key_environment']):raise ValueError('Invalid key environment variable')
    if value['security']['encryption']!='none' and value['packaging']['format']!='pak':raise ValueError('Encryption requires BPAK packaging')
    if value['security']['encrypt_manifests'] and value['security']['encryption']=='none':raise ValueError('Manifest encryption requires AES-256-GCM')
    for key in ('include_assets','exclude_assets'):
        patterns=value['packaging'][key]
        if len(patterns)>65536:raise ValueError('Asset rules exceed size limit')
        for pattern in patterns.splitlines():
            pattern=pattern.strip()
            if not pattern:continue
            if len(pattern)>4096 or pattern.startswith('/') or any(c in pattern for c in '\\:\0') or '..' in pattern.split('/'):raise ValueError('Asset patterns must use safe project-relative paths')
    return Effective(value)
def Save(project,value):
    value=Validate(value);path=PathFor(project);temporary=path.with_suffix('.json.tmp');temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(path)
def ApplyPreset(value,preset):
    value=Merge(value);settings=PRESETS[preset];value['security'].update({k:v for k,v in settings.items() if k in value['security']});value['security']['preset']=preset
    selected=value['target']['platform']
    if 'platforms' in value:value['platforms'][selected]['key_storage']=settings['key_storage']
    value['packaging'].update({k:v for k,v in settings.items() if k in value['packaging']})
    if settings['encryption']!='none':value['packaging']['format']='pak'
    return value
