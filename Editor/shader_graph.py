"""Versioned shader graphs. No Python or arbitrary source is evaluated from assets."""
from __future__ import annotations
import copy,json,math,re,uuid
from dataclasses import dataclass
from pathlib import Path
from .materials import MaterialError,_Write

@dataclass(frozen=True)
class NodeSpec:
    Category:str
    Inputs:tuple
    Expression:str
    Output:str='same'

NODES={}
def _Add(name,category,inputs,expression,output='same'):
    NODES[name]=NodeSpec(category,tuple(inputs),expression,output)

for name,expression in {'Add':'({a}+{b})','Subtract':'({a}-{b})','Multiply':'({a}*{b})','Divide':'({a}/{b})','Power':'pow({a},{b})','Minimum':'min({a},{b})','Maximum':'max({a},{b})','Modulo':'mod({a},{b})','Step':'step({a},{b})','Atan2':'atan({a},{b})'}.items():_Add(name,'Math',('a','b'),expression)
for name,function in {'Sine':'sin','Cosine':'cos','Tangent':'tan','ArcSine':'asin','ArcCosine':'acos','ArcTangent':'atan','Absolute':'abs','Sign':'sign','Floor':'floor','Ceil':'ceil','Round':'round','Fraction':'fract','SquareRoot':'sqrt','InverseSquareRoot':'inversesqrt','Exponential':'exp','Exponential2':'exp2','Logarithm':'log','Logarithm2':'log2','Normalize':'normalize','Radians':'radians','Degrees':'degrees'}.items():_Add(name,'Math',('x',),function+'({x})')
for name,expression in {'Negate':'(-{x})','OneMinus':'(1.0-{x})','Reciprocal':'(1.0/{x})','Saturate':'clamp({x},0.0,1.0)','Square':'({x}*{x})'}.items():_Add(name,'Math',('x',),expression)
_Add('Lerp','Math',('a','b','t'),'mix({a},{b},{t})')
_Add('Clamp','Math',('x','minimum','maximum'),'clamp({x},{minimum},{maximum})')
_Add('Smoothstep','Math',('minimum','maximum','x'),'smoothstep({minimum},{maximum},{x})')
_Add('Remap','Math',('x','inMin','inMax','outMin','outMax'),'({outMin}+({x}-{inMin})/({inMax}-{inMin})*({outMax}-{outMin}))')
_Add('Length','Vector',('x',),'length({x})','float')
_Add('Distance','Vector',('a','b'),'distance({a},{b})','float')
_Add('Dot','Vector',('a','b'),'dot({a},{b})','float')
_Add('Cross','Vector',('a','b'),'cross({a},{b})','float3')
_Add('Reflect','Vector',('direction','normal'),'reflect({direction},{normal})','float3')
_Add('Refract','Vector',('direction','normal','eta'),'refract({direction},{normal},{eta})','float3')
for size in (2,3,4):_Add('Vector'+str(size),'Vector',tuple('xyzw'[:size]),'vec'+str(size)+'('+','.join('{'+x+'}' for x in 'xyzw'[:size])+')','float'+str(size))
for name,kind in [('Float','float'),('Vector2Value','float2'),('Vector3Value','float3'),('Color','float4'),('Vector4Value','float4'),('Property','property'),('Texture','texture')]:_Add(name,'Input',(),'value',kind)
for name,expression,kind in [('UV','UV','float2'),('WorldPosition','worldPosition','float3'),('WorldNormal','worldNormal','float3'),('VertexColor','vertexColor','float4'),('Time','time','float'),('Pi','3.141592653589793','float'),('Tau','6.283185307179586','float')]:_Add(name,'Input',(),expression,kind)
_Add('SampleTexture','Texture',('texture','uv'),'texture({texture},{uv})','float4')
_Add('Swizzle','Vector',('x',),'swizzle')
_Add('TilingOffset','UV',('uv','tiling','offset'),'({uv}*{tiling}+{offset})','float2')
_Add('RotateUV','UV',('uv','angle','center'),'({center}+mat2(cos({angle}),sin({angle}),-sin({angle}),cos({angle}))*({uv}-{center}))','float2')
_Add('Panner','UV',('uv','speed','time'),'({uv}+{speed}*{time})','float2')
_Add('Polar','UV',('uv',),'vec2(length({uv}),atan(({uv}).y,({uv}).x))','float2')
_Add('Checker','Procedural',('uv','scale'),'mod(floor(({uv}).x*{scale})+floor(({uv}).y*{scale}),2.0)','float')
_Add('Noise','Procedural',('uv',),'fract(sin(dot({uv},vec2(12.9898,78.233)))*43758.5453)','float')
_Add('Gradient','Procedural',('uv',),'({uv}).x','float')
_Add('RadialGradient','Procedural',('uv',),'length({uv}-vec2(0.5))','float')
_Add('Ellipse','Procedural',('uv','size'),'(1.0-step(1.0,length(({uv}-vec2(0.5))*2.0/{size})))','float')
_Add('Rectangle','Procedural',('uv','size'),'(step(abs(({uv}).x-0.5),({size}).x*0.5)*step(abs(({uv}).y-0.5),({size}).y*0.5))','float')
_Add('Fresnel','Lighting',('normal','view','power'),'pow(1.0-clamp(dot(normalize({normal}),normalize({view})),0.0,1.0),{power})','float')
_Add('NormalUnpack','Lighting',('color',),'normalize(({color}).xyz*2.0-1.0)','float3')
_Add('Luminance','Color',('color',),'dot(({color}).rgb,vec3(0.2126,0.7152,0.0722))','float')
_Add('Desaturate','Color',('color','amount'),'mix({color},vec3(dot({color},vec3(0.2126,0.7152,0.0722))),{amount})','float3')
_Add('Contrast','Color',('color','contrast'),'(({color}-0.5)*{contrast}+0.5)','float3')
_Add('Posterize','Color',('color','steps'),'(floor({color}*{steps})/{steps})','float3')
_Add('SRGBToLinear','Color',('color',),'pow(max({color},vec3(0.0)),vec3(2.2))','float3')
_Add('LinearToSRGB','Color',('color',),'pow(max({color},vec3(0.0)),vec3(1.0/2.2))','float3')
for name,operator in [('Less','<'),('Greater','>'),('Equal','=='),('LessEqual','<='),('GreaterEqual','>=')]:_Add(name,'Logic',('a','b'),'({a}'+operator+'{b})','bool')
_Add('Branch','Logic',('condition','true','false'),'({condition}?{true}:{false})')
_Add('Boolean','Input',(),'value','bool')
_Add('Integer','Input',(),'value','int')
for name,expression in [('And','({a}&&{b})'),('Or','({a}||{b})'),('Not','(!{a})')]:_Add(name,'Logic',('a',) if name=='Not' else ('a','b'),expression,'bool')
for name,inputs,expression in [
    ('InverseLerp',('a','b','x'),'(({x}-{a})/({b}-{a}))'),
    ('SmoothMinimum',('a','b','k'),'(-log(exp(-{k}*{a})+exp(-{k}*{b}))/{k})'),
    ('PingPong',('x','length'),'({length}-abs(mod({x},2.0*{length})-{length}))'),
    ('TriangleWave',('x',),'abs(fract({x})*2.0-1.0)'),
    ('SawtoothWave',('x',),'(fract({x})*2.0-1.0)'),
    ('SquareWave',('x',),'step(0.5,fract({x}))'),
    ('Truncate',('x',),'trunc({x})'),
    ('HyperbolicSine',('x',),'sinh({x})'),
    ('HyperbolicCosine',('x',),'cosh({x})'),
    ('HyperbolicTangent',('x',),'tanh({x})')]:_Add(name,'Math',inputs,expression)
_Add('CameraPosition','Input',(),'cameraPosition','float3')
_Add('ViewDirection','Input',(),'normalize(cameraPosition-worldPosition)','float3')
_Add('ObjectPosition','Input',(),'(modelMatrix*vec4(0.0,0.0,0.0,1.0)).xyz','float3')
_Add('ObjectToWorld','Transform',('position',),'(modelMatrix*vec4({position},1.0)).xyz','float3')
_Add('WorldToObject','Transform',('position',),'(inverse(modelMatrix)*vec4({position},1.0)).xyz','float3')
_Add('TransformDirection','Transform',('direction',),'(modelMatrix*vec4({direction},0.0)).xyz','float3')
_Add('TransformNormal','Transform',('normal',),'normalize(transpose(inverse(mat3(modelMatrix)))*{normal})','float3')
_Add('RotateAxis','Vector',('direction','axis','angle'),'({direction}*cos({angle})+cross(normalize({axis}),{direction})*sin({angle})+normalize({axis})*dot(normalize({axis}),{direction})*(1.0-cos({angle})))','float3')
_Add('ParallaxUV','UV',('uv','height','view'),'({uv}+({view}).xy/max(abs(({view}).z),0.0001)*{height})','float2')
_Add('TextureLod','Texture',('texture','uv','lod'),'textureLod({texture},{uv},{lod})','float4')
_Add('HashNoise','Procedural',('uv',),'fract(sin(dot({uv},vec2(12.9898,78.233)))*43758.5453)','float')
_hash=lambda p:'fract(sin(dot('+p+',vec2(12.9898,78.233)))*43758.5453)'
NODES['Noise']=NodeSpec('Procedural',('uv',),'mix(mix('+_hash('floor({uv})')+','+_hash('floor({uv})+vec2(1.0,0.0)')+',smoothstep(0.0,1.0,fract({uv})).x),mix('+_hash('floor({uv})+vec2(0.0,1.0)')+','+_hash('floor({uv})+vec2(1.0,1.0)')+',smoothstep(0.0,1.0,fract({uv})).x),smoothstep(0.0,1.0,fract({uv})).y)','float')
_Add('Ring','Procedural',('uv','radius','width'),'(1.0-step({width},abs(length({uv}-vec2(0.5))-{radius})))','float')
_Add('Vertex','Output',('offset',),'','vertex')
_Add('Material','Output',('color','roughness','metallic','normal','emissive','ao'),'','surface')

KINDS={'float':1,'float2':2,'float3':3,'float4':4,'int':1,'bool':0,'texture2d':-1}
VERTEX_ONLY={'ObjectPosition','ObjectToWorld','WorldToObject','TransformDirection','TransformNormal'}
DEFAULTS={'color':[.65,.65,.65,1.0],'roughness':.5,'metallic':0.,'emissive':[0.,0.,0.,0.],'ao':1.,'uv':[0.,0.],'tiling':[1.,1.],'offset':[0.,0.],'speed':[.1,0.],'center':[.5,.5],'size':[.5,.5],'normal':[0.,0.,1.],'direction':[0.,0.,1.],'view':[0.,0.,1.],'eta':1.,'power':1.,'scale':8.,'contrast':1.,'steps':4.,'t':.5,'amount':.5,'maximum':1.,'inMax':1.,'outMax':1.,'condition':False}

def DefaultInput(node,name):
    if node['type']=='Boolean':return False
    if node['type'] in ('And','Or','Not'):return False
    if node['type']=='Vertex' and name=='offset':return [0.,0.,0.]
    if name=='axis':return [0.,1.,0.]
    if name=='position':return [0.,0.,0.]
    if name in ('length','k'):return 1.
    if name=='radius':return .25
    if name=='width':return .05
    if NODES[node['type']].Category=='Color' and name=='color' and node['type']!='Luminance':return [.65,.65,.65]
    if node['type']=='Cross' and name in ('a','b'):return [1.,0.,0.] if name=='a' else [0.,1.,0.]
    if node['type']=='Swizzle' and name=='x':return [0.,0.,0.,1.]
    return copy.deepcopy(DEFAULTS.get(name,0.))

def NewGraph():
    return {'version':1,'name':'GraphShader','shading':'lit','blending':'opaque','doubleSided':False,'nodes':[{'id':'output','type':'Material','position':[460,80],'values':{},'inputs':{}}]}

def ReadSource(path):
    with Path(path).open('rb') as stream:source=stream.read(4*1024*1024+1)
    if len(source)>4*1024*1024:raise MaterialError('Shader graph exceeds 4 MiB')
    return source

def Load(path):
    return ParseSource(ReadSource(path))

def ParseSource(source):
    try:graph=json.loads(source.decode('utf-8-sig'))
    except RecursionError:raise MaterialError('Shader graph JSON nesting exceeds limits') from None
    Validate(graph);return graph

def Validate(graph):
    if not isinstance(graph,dict) or graph.get('version')!=1:raise MaterialError('Unsupported shader graph version')
    pending=[(graph,0)];count=0
    while pending:
        item,depth=pending.pop();count+=1
        if depth>16 or count>100000:raise MaterialError('Shader graph document nesting or size exceeds limits')
        if isinstance(item,dict):
            if len(item)>4096 or any(not isinstance(key,str) or len(key)>128 for key in item):raise MaterialError('Invalid graph object fields')
            pending.extend((child,depth+1) for child in item.values())
        elif isinstance(item,list):
            if len(item)>4096:raise MaterialError('Graph array exceeds limits')
            pending.extend((child,depth+1) for child in item)
        elif isinstance(item,str):
            if len(item)>8192:raise MaterialError('Graph string exceeds limits')
        elif isinstance(item,(float,int)):
            if abs(item)>1e30 or not math.isfinite(item):raise MaterialError('Graph numbers must be finite and bounded')
        elif item is not None:raise MaterialError('Invalid graph document value')
    if not isinstance(graph.get('name'),str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,63}',graph['name']):raise MaterialError('Invalid graph shader name')
    if graph.get('shading','lit') not in ('lit','unlit') or graph.get('blending','opaque') not in ('opaque','transparent','masked','add'):raise MaterialError('Invalid graph shading options')
    if not isinstance(graph.get('doubleSided',False),bool):raise MaterialError('Double sided must be boolean')
    nodes=graph.get('nodes');ids=set()
    if not isinstance(nodes,list) or not 1<=len(nodes)<=512:raise MaterialError('A graph must have 1–512 nodes')
    for node in nodes:
        if not isinstance(node,dict) or not isinstance(node.get('type'),str) or node['type'] not in NODES:raise MaterialError('Unknown shader node')
        identity=node.get('id')
        if not isinstance(identity,str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',identity) or identity in ids:raise MaterialError('Duplicate or invalid node identity')
        ids.add(identity)
        if not isinstance(node.get('values',{}),dict) or not isinstance(node.get('inputs',{}),dict):raise MaterialError('Invalid node data')
        if len(node.get('values',{}))>32:raise MaterialError('Too many node values')
        def Value(value,depth=0):
            if depth>4:raise MaterialError('Node value nesting exceeds limits')
            if isinstance(value,str):
                if len(value)>256:raise MaterialError('Node value string exceeds limits')
            elif isinstance(value,(float,int)):
                if not math.isfinite(value) or abs(value)>1e30:raise MaterialError('Node value must be finite')
            elif isinstance(value,list):
                if len(value)>16:raise MaterialError('Node value array exceeds limits')
                for child in value:Value(child,depth+1)
            elif value is not None:raise MaterialError('Invalid node value')
        for value in node.get('values',{}).values():Value(value)
        for key in ('name','kind','mask','texture'):
            value=node.get('values',{}).get(key)
            if value is not None and not isinstance(value,str):raise MaterialError('Expected text for node '+key)
        position=node.get('position',[0,0])
        if not isinstance(position,list) or len(position)!=2 or any(not isinstance(x,(float,int)) or not math.isfinite(x) or abs(x)>1e6 for x in position):raise MaterialError('Invalid node position')
        if set(node.get('inputs',{}))-set(NODES[node['type']].Inputs):raise MaterialError('Unknown input socket')
    if sum(node['type']=='Material' for node in nodes)!=1:raise MaterialError('A graph requires exactly one Material output')
    if sum(node['type']=='Vertex' for node in nodes)>1:raise MaterialError('A graph permits only one Vertex output')
    for node in nodes:
        for source in node.get('inputs',{}).values():
            if not isinstance(source,str) or source not in ids:raise MaterialError('Missing source node')
    # Validate every node, including disconnected work, to reject hidden cycles.
    visiting=set();visited={};by_id={n['id']:n for n in nodes}
    def Visit(identity,depth=0):
        if depth>128:raise MaterialError('Shader graph depth exceeds 128 nodes')
        if identity in visiting:raise MaterialError('Shader graph contains a cycle')
        if identity in visited:return visited[identity]
        visiting.add(identity)
        length=1
        for source in by_id[identity].get('inputs',{}).values():length=max(length,Visit(source,depth+1)+1)
        if length>128:raise MaterialError('Shader graph depth exceeds 128 nodes')
        visiting.remove(identity);visited[identity]=length;return length
    for identity in by_id:Visit(identity)
    return graph

def Literal(value,kind=None):
    if isinstance(value,bool):return ('true' if value else 'false'),'bool'
    values=value if isinstance(value,(list,tuple)) else [value]
    if not 1<=len(values)<=4 or any(isinstance(x,bool) or not isinstance(x,(float,int)) or not math.isfinite(x) or abs(x)>1e30 for x in values):raise MaterialError('Expected finite scalar or vector values')
    actual='float'+(str(len(values)) if len(values)>1 else '')
    text=[format(float(x),'.9g') for x in values];text=[x if any(c in x for c in '.eE') else x+'.0' for x in text]
    return (text[0] if len(text)==1 else 'vec'+str(len(text))+'('+','.join(text)+')'),actual

def Convert(expression,source,target):
    if source==target:return expression
    if source=='int' and target=='float':return 'float('+expression+')'
    if source=='int' and target in ('float2','float3','float4'):return 'vec'+target[-1]+'(float('+expression+'))'
    if source=='float' and target in ('float2','float3','float4'):return 'vec'+target[-1]+'('+expression+')'
    raise MaterialError(f'Cannot connect {source} to {target}; use Vector or Swizzle nodes')

def Generate(graph):
    Validate(graph);nodes={n['id']:n for n in graph['nodes']};lines=[];properties={};compiled={};used_textures=set();stage='fragment'
    def Input(node,name,target=None):
        source=node.get('inputs',{}).get(name)
        if not source and name=='uv' and name not in node.get('values',{}):expression,kind='UV','float2'
        else:expression,kind=Build(source) if source else Literal(node.get('values',{}).get(name,DefaultInput(node,name)))
        return (Convert(expression,kind,target),target) if target else (expression,kind)
    def Build(identity):
        if identity in compiled:return compiled[identity]
        node=nodes[identity];name=node['type'];spec=NODES[name];values=node.get('values',{})
        if name in VERTEX_ONLY and stage!='vertex':raise MaterialError(name+' is available only in the Vertex stage')
        if spec.Output in ('surface','vertex'):raise MaterialError('Output nodes cannot be used as values')
        if name in ('Property','Texture'):
            if name=='Texture':used_textures.add(identity)
            parameter=values.get('name','GraphValue');kind='texture2d' if name=='Texture' else values.get('kind','float')
            if name=='Property' and kind=='texture2d':raise MaterialError('Use a Texture node for texture properties')
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,47}',parameter) or parameter.startswith('bzg_') or parameter in {'color','roughness','metallic','normal','emissive','ao','time','UV','worldPosition','worldNormal','vertexColor','position','cameraPosition','viewMatrix','projectionMatrix','modelMatrix'}:raise MaterialError('Invalid or reserved graph property name')
            if kind not in KINDS:raise MaterialError('Unsupported graph property type')
            if kind=='int':
                value=values.get('value',1)
                if isinstance(value,bool) or not isinstance(value,int) or not -2147483648<=value<=2147483647:raise MaterialError('Expected signed 32-bit integer')
                default=' = '+str(value)+';'
            else:default=';' if kind=='texture2d' else ' = '+Convert(*Literal(values.get('value',1. if kind=='float' else False if kind=='bool' else [1.]*KINDS[kind])),kind)+';'
            declaration=parameter+': '+({'float2':'vec2','float3':'vec3','float4':'vec4'}.get(kind,kind))+default
            if parameter in properties and properties[parameter]!=declaration:raise MaterialError('Conflicting graph property declarations: '+parameter)
            properties[parameter]=declaration;compiled[identity]=(parameter,kind);return compiled[identity]
        if spec.Output=='int':
            value=values.get('value',1)
            if isinstance(value,bool) or not isinstance(value,int) or not -2147483648<=value<=2147483647:raise MaterialError('Expected signed 32-bit integer')
            expression,kind=str(value),'int'
        elif spec.Expression=='value':expression,kind=Literal(values.get('value',False if spec.Output=='bool' else 1. if spec.Output=='float' else [1.]*KINDS[spec.Output]));expression=Convert(expression,kind,spec.Output);kind=spec.Output
        elif name=='Swizzle':
            value,kind=Input(node,'x');mask=values.get('mask','x')
            if not isinstance(mask,str) or not re.fullmatch('[xyzw]{1,4}|[rgba]{1,4}',mask) or kind not in KINDS or KINDS[kind]<2 or any('xyzw'.find(c if c in 'xyzw' else 'xyzw'['rgba'.index(c)])>=KINDS[kind] for c in mask):raise MaterialError('Invalid swizzle')
            expression='('+value+').'+mask;kind='float'+(str(len(mask)) if len(mask)>1 else '')
        elif not spec.Inputs:expression,kind=spec.Expression,spec.Output
        else:
            args={key:Input(node,key) for key in spec.Inputs}
            if name in ('SampleTexture','TextureLod'):
                if args['texture'][1]!='texture2d':raise MaterialError('SampleTexture requires a Texture node')
                args['uv']=(Convert(*args['uv'],'float2'),'float2')
                if name=='TextureLod':args['lod']=(Convert(*args['lod'],'float'),'float')
            elif name in ('And','Or','Not'):
                if any(v[1]!='bool' for v in args.values()):raise MaterialError('Logic inputs must be boolean')
            elif name.startswith('Vector'):
                args={k:(Convert(*v,'float'),'float') for k,v in args.items()}
            elif name=='Branch':
                if args['condition'][1]!='bool':raise MaterialError('Branch condition must be boolean')
                kind='float' if {args['true'][1],args['false'][1]}=={'int','float'} else args['true'][1]
                args['true']=(Convert(*args['true'],kind),kind);args['false']=(Convert(*args['false'],kind),kind)
            else:
                if any(value[1] in ('texture2d','bool') for value in args.values()):raise MaterialError('Expected numeric input')
                explicit={'uv':'float2','tiling':'float2','offset':'float2','speed':'float2','center':'float2','size':'float2','angle':'float','time':'float','eta':'float','normal':'float3','view':'float3','direction':'float3','axis':'float3','position':'float3','power':'float','scale':'float'}
                if name=='Cross':explicit.update(a='float3',b='float3')
                if spec.Category=='Color':explicit['color']='float3' if name not in ('Luminance',) else None
                for key,target in explicit.items():
                    if key in args and target:args[key]=(Convert(*args[key],target),target)
                # Scalar broadcasting is permitted; vector dimension mismatch is not.
                numeric=[k for k in args if k not in explicit and args[k][1]!='bool']
                if any(args[k][1] in ('texture2d','bool') for k in numeric):raise MaterialError('Expected numeric input')
                size=max((KINDS[args[k][1]] for k in numeric),default=1);common='float'+(str(size) if size>1 else '')
                if name in ('Less','Greater','Equal','LessEqual','GreaterEqual'):common='float'
                for key in numeric:args[key]=(Convert(*args[key],common),common)
            expression=spec.Expression.format(**{key:value[0] for key,value in args.items()})
            kind=spec.Output if spec.Output!='same' else (args['true'][1] if name=='Branch' else max((v[1] for v in args.values()),key=lambda k:KINDS.get(k,-1)))
            if name=='Branch':
                variable='bzg_'+str(len(lines));type_name={'float2':'vec2','float3':'vec3','float4':'vec4'}.get(kind,kind)
                lines.append(f'        {type_name} {variable};')
                lines.append('        if ('+args['condition'][0]+') { '+variable+' = '+args['true'][0]+'; } else { '+variable+' = '+args['false'][0]+'; }')
                compiled[identity]=(variable,kind);return compiled[identity]
        if kind=='texture2d':raise MaterialError('Texture must be sampled before numeric operations')
        variable='bzg_'+str(len(lines));type_name={'float2':'vec2','float3':'vec3','float4':'vec4'}.get(kind,kind)
        lines.append(f'        {type_name} {variable} = {expression};');compiled[identity]=(variable,kind);return compiled[identity]
    output=next(n for n in nodes.values() if n['type']=='Material')
    for name,target in [('color','float4'),('roughness','float'),('metallic','float'),('normal','float3'),('emissive','float4'),('ao','float')]:
        if graph.get('shading','lit')=='unlit' and name in ('roughness','metallic','normal','ao'):continue
        if name=='normal' and name not in output.get('inputs',{}):continue
        expression,_=Input(output,name,target);lines.append('        '+name+' = '+expression+';')
    fragment=lines;lines=[];compiled.clear();vertex='';stage='vertex'
    vertex_output=next((n for n in nodes.values() if n['type']=='Vertex'),None)
    if vertex_output:
        expression,_=Input(vertex_output,'offset','float3');lines.append('        position = position + '+expression+';');vertex='    vertex {\n'+'\n'.join(lines)+'\n    }\n'
    lines=fragment
    options='shading: '+graph.get('shading','lit')+'; blending: '+graph.get('blending','opaque')+'; doubleSided: '+('true' if graph.get('doubleSided',False) else 'false')+';'
    defaults={}
    for node in nodes.values():
        if node['type']!='Texture' or node['id'] not in used_textures:continue
        values=node.get('values',{});name=values.get('name','GraphValue')
        if name not in properties:continue
        try:identity=str(uuid.UUID(str(values.get('texture') or uuid.UUID(int=0))))
        except ValueError:raise MaterialError('Invalid texture asset identity') from None
        if name in defaults and defaults[name]!=identity:raise MaterialError('Conflicting graph texture defaults')
        defaults[name]=identity
    metadata=''.join('// BazzaltTextureDefault '+name+' '+identity+'\n' for name,identity in sorted(defaults.items()))
    return metadata+'shader '+graph['name']+' {\n    options { '+options+' }\n    properties {\n        '+'\n        '.join(properties.values())+'\n    }\n'+vertex+'    material {\n'+'\n'.join(lines)+'\n    }\n}\n'

def Save(path,graph):
    Validate(graph);path=Path(path);text=json.dumps(graph,indent=2,ensure_ascii=False)+'\n'
    if len(text.encode('utf-8'))>4*1024*1024:raise MaterialError('Shader graph exceeds 4 MiB')
    if path.is_file() and path.read_text(encoding='utf-8')==text:return False
    _Write(path,text);return True

def AddNode(graph,kind,position=(0,0)):
    if kind not in NODES or NODES[kind].Output=='surface':raise MaterialError('Invalid added node')
    node={'id':uuid.uuid4().hex,'type':kind,'position':list(position),'values':{},'inputs':{}}
    graph['nodes'].append(node);return node

def Unlock(path):
    """Destructively convert source while retaining the asset's UUID sidecar."""
    path=Path(path);target=path.with_suffix('.shad')
    if target.exists() or Path(str(target)+'.meta').exists():raise MaterialError('Shader destination already exists')
    source=Generate(Load(path));_Write(target,source)
    old_meta=Path(str(path)+'.meta');new_meta=Path(str(target)+'.meta')
    try:
        if old_meta.exists():old_meta.rename(new_meta)
        path.unlink()
    except OSError:
        if new_meta.exists() and not old_meta.exists():new_meta.rename(old_meta)
        target.unlink(missing_ok=True);raise
    return target
