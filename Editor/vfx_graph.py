"""Versioned, data-only particle flow graphs; imports refer to asset UUIDs."""
import copy,json,math,uuid
from dataclasses import dataclass
from pathlib import Path
from .materials import _Write
@dataclass(frozen=True)
class NodeSpec:
    Category:str
    Inputs:tuple=('in',)
    Expression:str=''
    Output:str='flow'
DEFAULTS={
 'Spawn':dict(rate=20.,duration=5.,delay=0.,burst=0,capacity=4096,loop=True),
 'Initialize':dict(lifetimeMin=1.,lifetimeMax=2.,speedMin=1.,speedMax=2.,sizeMin=.1,sizeMax=.2,cone=20.,angularSpeed=0.),
 'Box':dict(extents=[.5,.5,.5]),'Sphere':dict(radius=.5),'Velocity':dict(velocity=[0.,0.,0.]),
 'Gravity':dict(acceleration=[0.,-9.81,0.]),'Drag':dict(coefficient=1.),'Turbulence':dict(strength=1.,frequency=1.),
 'ColorOverLife':dict(start=[1.,1.,1.,1.],end=[1.,1.,1.,0.]),'SizeOverLife':dict(endMultiplier=0.),
 'Flipbook':dict(columns=1,rows=1),'Import':dict(asset='00000000-0000-0000-0000-000000000000'),
 'Output':dict(material='00000000-0000-0000-0000-000000000000',texture='00000000-0000-0000-0000-000000000000')}
RANGES={'rate':(0,100000),'duration':(.01,3600),'delay':(0,3600),'burst':(0,65535),'capacity':(1,32768),'lifetimeMin':(.000001,600),'lifetimeMax':(.000001,600),'sizeMin':(0,10000),'sizeMax':(0,10000),'cone':(0,180),'coefficient':(0,1000000),'strength':(0,1000000),'frequency':(0,1000000),'radius':(0,1000000),'endMultiplier':(0,10000),'columns':(1,64),'rows':(1,64)}
NODES={name:NodeSpec('Spawn' if name=='Spawn' else 'Initialize' if name in ('Initialize','Box','Sphere','Velocity') else 'Assets' if name=='Import' else 'Output' if name=='Output' else 'Update',() if name=='Spawn' else ('in',),Output='surface' if name=='Output' else 'flow') for name in DEFAULTS}
def NewGraph():
    graph=dict(version=1,name='ParticleEffect',nodes=[]);previous=None
    for kind in ('Spawn','Initialize','Gravity','ColorOverLife','Output'):
        node=AddNode(graph,kind,(len(graph['nodes'])*230,80))
        if previous:node['inputs']['in']=previous
        previous=node['id']
    return graph

def AddNode(graph,kind,position=(0,0)):
    if kind not in NODES:raise ValueError('Unknown VFX node')
    node=dict(id=uuid.uuid4().hex,type=kind,position=list(position),inputs={},values=copy.deepcopy(DEFAULTS[kind]));graph['nodes'].append(node);return node

def Validate(graph):
    if not isinstance(graph,dict) or type(graph.get('version')) is not int or graph.get('version')!=1:raise ValueError('Unsupported VFX graph version')
    nodes=graph.get('nodes');index={}
    if not isinstance(nodes,list) or len(nodes)>256:raise ValueError('VFX supports at most 256 nodes per file')
    for n in nodes:
        if not isinstance(n,dict) or n.get('type') not in NODES or not isinstance(n.get('id'),str) or not 0<len(n['id'])<=64 or n['id'] in index:raise ValueError('Invalid or duplicate VFX node')
        if not isinstance(n.get('inputs'),dict) or set(n['inputs'])-set(NODES[n['type']].Inputs):raise ValueError('Invalid VFX flow input')
        values=n.get('values');defaults=DEFAULTS[n['type']]
        if not isinstance(values,dict) or set(values)-set(defaults):raise ValueError('Unknown VFX property')
        for key,value in values.items():
            default=defaults[key]
            if isinstance(default,str):
                if not isinstance(value,str):raise ValueError('VFX asset references must be UUIDs')
                try:uuid.UUID(value)
                except (ValueError,AttributeError) as e:raise ValueError('Invalid VFX asset UUID') from e
            elif isinstance(default,list):
                if not isinstance(value,list) or len(value)!=len(default) or any(type(v) not in (float,int) or not math.isfinite(v) or abs(v)>1e6 for v in value):raise ValueError('Invalid VFX vector')
            elif isinstance(default,bool):
                if type(value)!=bool:raise ValueError('Invalid VFX boolean')
            elif type(value) not in (float,int) or not math.isfinite(value) or abs(value)>1e6 or isinstance(default,int) and int(value)!=value:raise ValueError('Invalid VFX number')
        pos=n.get('position',[0,0])
        if not isinstance(pos,list) or len(pos)!=2 or any(type(v) not in (float,int) or not math.isfinite(v) or abs(v)>1e6 for v in pos):raise ValueError('Invalid VFX node position')
        index[n['id']]=n
    if sum(n['type']=='Output' for n in nodes)!=1:raise ValueError('VFX needs exactly one Output')
    active=set();done=set()
    def Visit(identity):
        if identity in done:return
        if identity in active:raise ValueError('VFX flow cycle')
        if identity not in index:raise ValueError('Missing VFX input node')
        active.add(identity)
        for source in index[identity]['inputs'].values():
            if not isinstance(source,str):raise ValueError('Invalid VFX input id')
            Visit(source)
        active.remove(identity);done.add(identity)
    for identity in index:Visit(identity)
    return graph

def Generate(graph):Validate(graph);return json.dumps(graph,ensure_ascii=False,separators=(',',':'),allow_nan=False)
def ReadSource(path):
    with Path(path).open('rb') as stream:data=stream.read(1024*1024+1)
    if len(data)>1024*1024:raise ValueError('VFX exceeds 1 MiB')
    return data

def ParseSource(source):
    try:return Validate(json.loads(source))
    except (json.JSONDecodeError,UnicodeError,RecursionError) as e:raise ValueError('Invalid VFX document') from e

def Load(path):return ParseSource(ReadSource(path))
def Save(path,graph):
    source=Generate(graph)+'\n';path=Path(path)
    if len(source.encode('utf-8'))>1024*1024:raise ValueError('VFX exceeds 1 MiB')
    if path.exists() and path.read_text(encoding='utf-8')==source:return False
    _Write(path,source);return True
