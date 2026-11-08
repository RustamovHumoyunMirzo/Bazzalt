"""Behavior tree XML contract shared by the block editor and hand-authored assets."""
from pathlib import Path
import math
import os
import tempfile
import xml.etree.ElementTree as ET

# kind: (category, attributes with defaults, child cardinality)
NODES = {}
def _nodes(category, names, fields=(), children=(0, 0)):
    for name in names.split():NODES[name]=(category, dict(fields), children)
_nodes('Composites','Sequence Selector ReactiveSequence ReactiveSelector Parallel Utility',children=(1,128))
_nodes('Decorators','Invert AlwaysSucceed AlwaysFail',children=(1,1))
_nodes('Decorators','Repeat', [('count',3)], (1,1))
_nodes('Decorators','Timeout Cooldown', [('seconds',1.0)], (1,1))
_nodes('Utility','Option',[('key','score'),('weight',1.0)],(1,1))
_nodes('Actions','Succeed Fail Stop Wake Sleep')
_nodes('Actions','Wait',[('seconds',1.0)])
_nodes('Actions','Action Condition',[('name','custom')])
_nodes('Blackboard','SetBool',[('key','flag'),('value',True)])
_nodes('Blackboard','SetNumber AddNumber',[('key','value'),('value',1.0)])
_nodes('Blackboard','SetString',[('key','text'),('value','hello')])
_nodes('Blackboard','SetVector',[('key','target'),('x',0.0),('y',0.0),('z',0.0)])
_nodes('Blackboard','HasKey ClearKey IsTrue',[('key','flag')])
_nodes('Blackboard','Compare',[('key','value'),('op','>='),('value',0.0)])
_nodes('Physics','SetVelocity ApplyForce ApplyImpulse',[('x',0.0),('y',0.0),('z',0.0)])
_nodes('Physics','Raycast',[('key','hit'),('x',0.0),('y',-1.0),('z',0.0),('distance',10.0)])
_nodes('Physics','OverlapSphere',[('key','nearby'),('radius',5.0)])
_nodes('Navigation','MoveTo',[('key','target'),('speed',3.0),('radius',0.25)])
_nodes('Navigation','WithinDistance',[('key','target'),('radius',2.0)])

def Parse(source):
    if len(source.encode('utf-8'))>1024*1024:raise ValueError('Behavior tree exceeds 1 MiB')
    if '<!DOCTYPE' in source.upper() or '<!ENTITY' in source.upper():raise ValueError('XML declarations are not supported')
    try:root=ET.fromstring(source)
    except ET.ParseError as error:raise ValueError(str(error)) from error
    if root.tag!='BehaviorTree' or root.get('version')!='1':raise ValueError('Expected BehaviorTree version="1"')
    if root.get('locked','true') not in ('true','false'):raise ValueError('Invalid locked flag')
    if any(c.tag not in ('Tree','Blackboard') for c in root):raise ValueError('Unknown behavior document section')
    trees=root.findall('Tree')
    if len(trees)!=1 or len(trees[0])!=1:raise ValueError('Tree requires exactly one root behavior')
    if len(root.findall('Blackboard'))>1:raise ValueError('Duplicate Blackboard')
    keys=set()
    for entry in root.find('Blackboard') if root.find('Blackboard') is not None else ():
        key=entry.get('key','')
        if not key or key in keys or len(entry):raise ValueError('Invalid or duplicate blackboard key')
        keys.add(key)
        if entry.tag not in ('Bool','Number','String','Vector'):raise ValueError('Invalid blackboard type')
        if entry.tag=='Bool' and entry.get('value','false') not in ('true','false'):raise ValueError('Invalid blackboard boolean')
        for field in ('value',) if entry.tag=='Number' else ('x','y','z') if entry.tag=='Vector' else ():
            try:number=float(entry.get(field,'0'))
            except ValueError:raise ValueError('Invalid blackboard number')
            if not math.isfinite(number):raise ValueError('Non-finite blackboard number')
    count=0
    def validate(node,depth=0):
        nonlocal count
        count+=1
        if depth>64 or count>4096:raise ValueError('Behavior tree is too complex')
        if node.tag not in NODES:raise ValueError('Unknown behavior: '+node.tag)
        _,fields,(lo,hi)=NODES[node.tag]
        if not lo<=len(node)<=hi:raise ValueError(f'{node.tag} requires {lo}..{hi} children')
        if node.tag=='Utility' and any(c.tag!='Option' for c in node):raise ValueError('Utility children must be Option blocks')
        for key,default in fields.items():
            value=node.get(key,str(default).lower() if isinstance(default,bool) else str(default))
            if isinstance(default,bool) and value not in ('true','false'):raise ValueError('Invalid boolean '+key)
            if isinstance(default,(int,float)) and not isinstance(default,bool):
                try:number=float(value)
                except ValueError:raise ValueError('Invalid number '+key)
                if not math.isfinite(number):raise ValueError('Non-finite '+key)
                if key in ('seconds','radius','distance','speed','count') and number<0:raise ValueError('Negative '+key)
                if key=='count' and (not number.is_integer() or number>1000000):raise ValueError('Invalid repeat count')
        if node.tag=='Compare' and node.get('op','>=') not in ('==','!=','>','>=','<','<='):raise ValueError('Invalid comparison')
        for child in node:validate(child,depth+1)
    validate(trees[0][0])
    return root

def Source(root):
    ET.indent(root,space='  ')
    source=ET.tostring(root,encoding='unicode')+'\n'
    Parse(source)
    return source

def NewTree():return '<BehaviorTree version="1" locked="true"><Tree><Sequence><Wait seconds="1"/><Succeed/></Sequence></Tree></BehaviorTree>\n'
def Load(path):return Parse(Path(path).read_text(encoding='utf-8'))
def Save(path,source):
    Parse(source);path=Path(path)
    with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,delete=False) as stream:stream.write(source);name=stream.name
    try:os.replace(name,path)
    finally:Path(name).unlink(missing_ok=True)

def RegisterBlocks(workspace):
    from .blockworkspace import BlockType,StringInput,FloatInput,IntInput,BoolInput,StatementInput,LabelInput
    blocks=[]
    for name,(category,fields,cardinality) in NODES.items():
        inputs=[LabelInput(name)]
        for key,value in fields.items():
            cls=BoolInput if isinstance(value,bool) else IntInput if isinstance(value,int) else FloatInput if isinstance(value,float) else StringInput
            inputs.append(cls(key,value,Label=key))
        if cardinality[1]:inputs.append(StatementInput('children',Label='Behaviors'))
        blocks.append(type(name+'Block',(BlockType,),dict(Type=name,DisplayName=name,Category=category,Inputs=inputs)))
    workspace.RegisterBlocks(blocks)

def LocalizeBlocks(workspace,locale):
    """Change presentation only; stable node IDs and XML attributes never translate."""
    tr=locale.Translate
    workspace.CategoryLabel=lambda name:tr('behavior.category.'+name) if name in {spec[0] for spec in NODES.values()} else name
    for definition in workspace.Registry.All():
        name=definition.TypeId
        definition.DisplayName=tr('behavior.block.'+name)
        definition.Tooltip=tr('behavior.tooltip.'+name)
        definition.Instance.DisplayName=definition.DisplayName
        definition.Inputs[0].Text=definition.DisplayName
        for field in definition.Inputs[1:]:field.Label=tr('behavior.field.'+field.Name)
    workspace.Canvas.SetTheme(workspace.Theme)
    workspace.Toolbox.Refresh()

def ToWorkspace(root):
    workspace=ET.Element('workspace',version='1')
    def block(parent,node):
        item=ET.SubElement(parent,'block',type=node.tag)
        if node.get('id'):item.set('id',node.get('id'))
        if node.get('collapsed')=='true':item.set('collapsed','true')
        if node.get('comment'):ET.SubElement(item,'comment',open=node.get('comment-open','true')).text=node.get('comment')
        for key,default in NODES[node.tag][1].items():ET.SubElement(item,'field',name=key).text=node.get(key,str(default).lower() if isinstance(default,bool) else str(default))
        if len(node):
            slot=ET.SubElement(item,'statement',name='children')
            for child in node:block(slot,child)
    node=root.find('Tree')[0]
    fields=NODES[node.tag][1]
    stack=ET.SubElement(workspace,'stack',x=node.get('editor-x',node.get('x','60') if 'x' not in fields else '60'),y=node.get('editor-y',node.get('y','60') if 'y' not in fields else '60'));block(stack,node)
    return ET.tostring(workspace,encoding='unicode')

def FromWorkspace(workspace,prior=None):
    root=ET.Element('BehaviorTree',version='1',locked='true')
    if prior is not None and prior.find('Blackboard') is not None:
        import copy
        root.append(copy.deepcopy(prior.find('Blackboard')))
    tree=ET.SubElement(root,'Tree')
    def node(block):
        result=ET.Element(block.Type,id=block.Id)
        if block.Collapsed:result.set('collapsed','true')
        if block.Comment:result.set('comment',block.Comment);result.set('comment-open',str(block.CommentOpen).lower())
        for key,value in block.Values.items():result.set(key,str(value).lower() if isinstance(value,bool) else str(value))
        child=block.Children.get('children')
        while child is not None:result.append(node(child));child=child.Next
        return result
    roots=workspace.Document.SortedRoots()
    if len(roots)!=1 or roots[0].Next:raise ValueError('Connect all blocks under one root behavior')
    if any(b.Disabled for b in workspace.Blocks()):raise ValueError('Remove disabled behaviors before saving')
    item=node(roots[0]);item.set('editor-x',str(roots[0].X));item.set('editor-y',str(roots[0].Y));tree.append(item)
    Source(root)
    return root
