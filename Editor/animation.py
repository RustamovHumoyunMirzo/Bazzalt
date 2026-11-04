"""Animation controllers with immutable imported clips and editable clip overrides."""
import json,math,uuid
from pathlib import Path
from .materials import _Write

def NewController():
    return dict(version=1,entry='Idle',states=[dict(name='Idle',clip='',speed=1.,loop=True)],transitions=[])

def Validate(data):
    if not isinstance(data,dict) or type(data.get('version')) is not int or data['version']!=1:raise ValueError('Unsupported animation controller version')
    clips=data.get('clips',[])
    if not isinstance(clips,list) or len(clips)>1024:raise ValueError('Invalid authored clip count')
    names=set()
    for clip in clips:
        ValidateClip(clip)
        if clip['name'] in names:raise ValueError('Duplicate authored clip')
        names.add(clip['name'])
    states=data.get('states');transitions=data.get('transitions',[])
    if not isinstance(states,list) or not 1<=len(states)<=256 or not isinstance(transitions,list) or len(transitions)>1024:raise ValueError('Invalid animation graph size')
    names=set()
    def Number(value,low,high):
        if type(value) not in (int,float) or not math.isfinite(value) or not low<=value<=high:raise ValueError('Invalid animation number')
    for state in states:
        name=state.get('name')
        if not isinstance(name,str) or not 1<=len(name)<=128 or name in names:raise ValueError('Invalid or duplicate animation state')
        for key in ('clip','parameterX','parameterY'):
            if not isinstance(state.get(key,''),str) or len(state.get(key,''))>256:raise ValueError('Invalid animation state text')
        position=state.get('position',[0,0])
        if not isinstance(position,list) or len(position)!=2:raise ValueError('Invalid state position')
        for coordinate in position:Number(coordinate,-1e6,1e6)
        names.add(name);Number(state.get('speed',1),0,100);blend=state.get('blend',0)
        if type(blend) is not int or blend not in (0,1,2) or type(state.get('loop',True)) is not bool:raise ValueError('Invalid animation state options')
        samples=state.get('samples',[])
        if not isinstance(samples,list) or len(samples)>64:raise ValueError('Invalid blend samples')
        if blend and (not samples or not state.get('parameterX') or blend==2 and not state.get('parameterY')):raise ValueError('Blend tree requires parameters and samples')
        for sample in samples:
            if not isinstance(sample.get('clip'),str) or not sample['clip']:raise ValueError('Blend sample needs a clip')
            for key in ('x','y'):Number(sample.get(key,0),-1e6,1e6)
            Number(sample.get('speed',1),0,100)
    if data.get('entry') not in names:raise ValueError('Missing entry animation state')
    for transition in transitions:
        if transition.get('from') not in names|{'*'} or transition.get('to') not in names:raise ValueError('Missing transition state')
        Number(transition.get('duration',.2),0,60);Number(transition.get('exitTime',-1),-1,1000)
        conditions=transition.get('conditions',[])
        if not isinstance(conditions,list) or len(conditions)>16:raise ValueError('Invalid transition conditions')
        for row in conditions:
            if not row.get('parameter') or type(row.get('comparison',0)) is not int or row.get('comparison',0) not in range(4):raise ValueError('Invalid transition condition')
            Number(row.get('value',0),-1e30,1e30)
    if data.get('sourceModel'):uuid.UUID(data['sourceModel'])
    mappings=data.get('retarget',[])
    if not isinstance(mappings,list) or len(mappings)>4096:raise ValueError('Invalid retarget map')
    used=set()
    for mapping in mappings:
        for key in ('source','target'):
            if type(mapping.get(key)) is not int or not 0<=mapping[key]<4096:raise ValueError('Invalid retarget bone index')
        if mapping['target'] in used or type(mapping.get('copyTranslation',False)) is not bool:raise ValueError('Invalid retarget mapping')
        used.add(mapping['target'])
    Number(data.get('translationScale',1),.000001,1000)
    return data

def Load(path):
    raw=Path(path).read_bytes()
    if len(raw)>16*1024*1024:raise ValueError('Animation controller exceeds 16 MiB')
    return Validate(json.loads(raw))
def Save(path,data):
    Validate(data);path=Path(path);source=json.dumps(data,ensure_ascii=False,indent=2)+'\n'
    if len(source.encode('utf-8'))>16*1024*1024:raise ValueError('Animation controller exceeds 16 MiB')
    if path.exists() and path.read_text(encoding='utf-8')==source:return False
    _Write(path,source);return True


def ValidateClip(clip):
    if not isinstance(clip,dict) or not isinstance(clip.get('name'),str) or not 1<=len(clip['name'])<=128:raise ValueError('Invalid clip name')
    def Number(v,low=-1e30,high=1e30):
        if type(v) not in (int,float) or not math.isfinite(v) or not low<=v<=high:raise ValueError('Invalid clip number')
    duration=clip.get('duration',0);Number(duration,0,86400)
    tracks=clip.get('tracks',[]);markers=clip.get('markers',[])
    if not isinstance(tracks,list) or len(tracks)>16384 or not isinstance(markers,list) or len(markers)>4096:raise ValueError('Invalid clip size')
    used=set();count=0
    for track in tracks:
        for name,limit in (('bone',4096),('channel',4),('interpolation',3),('target',256)):
            value=track.get(name,1 if name=='interpolation' else 0)
            if type(value) is not int or not 0<=value<limit:raise ValueError('Invalid clip track')
        identity=(track.get('bone',0),track.get('channel',0),track.get('target',0) if track.get('channel',0)==3 else 0)
        if identity in used:raise ValueError('Duplicate clip channel')
        used.add(identity);previous=-1;keys=track.get('keys',[])
        if not isinstance(keys,list) or not keys:raise ValueError('Track needs keys')
        count+=len(keys)
        if count>1000000:raise ValueError('Clip key budget exceeded')
        for key in keys:
            time=key.get('time');Number(time,0,duration)
            if time<=previous:raise ValueError('Keys must be strictly increasing')
            previous=time
            for field in ('value','in','out'):
                value=key.get(field,[0,0,0,0])
                if not isinstance(value,list) or len(value)!=4:raise ValueError('Key needs four values')
                for v in value:Number(v)
            if identity[1]==1 and sum(v*v for v in key['value'])<1e-12:raise ValueError('Rotation quaternion cannot be zero')
    for marker in markers:
        Number(marker.get('time'),0,duration)
        if not isinstance(marker.get('name'),str) or not 1<=len(marker['name'])<=256:raise ValueError('Invalid marker')
    return clip
