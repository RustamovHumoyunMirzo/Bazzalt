"""Read declarative Lua metadata without executing project code."""
import math
import re
import ast
from Editor.property_rules import CheckRules,CheckTypeRules,RuleError

class DeclarationError(ValueError):pass

def _Tokens(text):
    pattern=re.compile(r"\s+|--\[(=*)\[[\s\S]*?\]\1\]|--[^\n]*|\[(=*)\[[\s\S]*?\]\2\]|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|.")
    return [m.group() for m in pattern.finditer(text) if not m.group().isspace() and not m.group().startswith('--')]

def _String(token):
    if len(token)<2 or token[0] not in "\"'" or token[-1]!=token[0]:raise DeclarationError('Declaration names and types must be quoted literals')
    if '\\' in token:raise DeclarationError('Declaration names and types cannot contain escapes')
    return token[1:-1]

def _Rules(tokens):
    if len(tokens)<2 or tokens[0]!='{' or tokens[-1]!='}':raise DeclarationError('PROPERTY options must be a literal table')
    rules={};parts=[];start=1
    for i in range(1,len(tokens)):
        if tokens[i] in (',','}'):
            if i>start:parts.append(tokens[start:i])
            start=i+1
    for part in parts:
        if len(part)<3 or part[1]!='=' or part[0] in rules:raise DeclarationError('Invalid or duplicate PROPERTY option')
        raw=''.join(part[2:])
        try:value={'true':True,'false':False}[raw] if raw in ('true','false') else ast.literal_eval(raw)
        except (ValueError,SyntaxError):raise DeclarationError('PROPERTY options must be literal values')
        rules[part[0]]=value
    try:return CheckRules(rules)
    except RuleError as error:raise DeclarationError(str(error)) from error

def ReadDeclarations(text):
    tokens=_Tokens(text);components=[];properties=[]
    for i,token in enumerate(tokens):
        if token not in ('COMPONENT','PROPERTY') or i+1>=len(tokens) or tokens[i+1]!='(':continue
        if i and tokens[i-1] in ('.',':','function'):continue
        depth=0;args=[];start=i+2;end=None
        for j in range(start,len(tokens)):
            value=tokens[j]
            if value in ('(','{','['):depth+=1
            elif value==')' and depth==0:args.append(tokens[start:j]);end=j;break
            elif value in (')','}',']'):depth-=1
            elif value==',' and depth==0:args.append(tokens[start:j]);start=j+1
        if end is None:raise DeclarationError('Unclosed Lua declaration')
        if token=='COMPONENT':
            if i<3 or tokens[i-1]!='=' or tokens[i-3]!='local' or len(args)!=1 or len(args[0])!=1:raise DeclarationError('Use local Name = COMPONENT("Name")')
            name=_String(args[0][0]);variable=tokens[i-2]
            if not re.fullmatch(r'[A-Za-z_]\w*',name):raise DeclarationError('Invalid Lua component name')
            components.append((variable,name))
        else:
            if i<4 or tokens[i-1]!='=' or tokens[i-3]!='.' or len(args) not in (2,3) or len(args[0])!=1:raise DeclarationError('Use Name.Field = PROPERTY("type", default, {options})')
            rules=_Rules(args[2]) if len(args)==3 else {}
            kind=_String(args[0][0]);default=''.join(args[1])
            try:CheckTypeRules(kind,rules)
            except RuleError as error:raise DeclarationError(str(error)) from error
            scalars={'float','double','int','bool','string','std::string'}
            vectors={'Vec2':2,'Vec3':3,'Vec4':4,'Quaternion':4}
            if kind in scalars:
                valid=(default in ('true','false') if kind=='bool' else bool(re.fullmatch(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',default)) if kind in ('string','std::string') else bool(re.fullmatch(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?',default)))
                if not valid:raise DeclarationError('Scalar PROPERTY defaults must be literal values')
                if kind=='int' and not re.fullmatch(r'[+-]?\d+',default):raise DeclarationError('Integer PROPERTY defaults must be integers')
                if kind in ('float','double','int') and (not math.isfinite(float(default)) or kind=='int' and not -2147483648<=int(default)<=2147483647):raise DeclarationError('Numeric PROPERTY default is out of range')
            elif kind in vectors:
                match=re.fullmatch(r'(?:B|Bazzalt)\.'+kind+r'\.new\((.*)\)',default)
                values=match[1].split(',') if match else []
                if len(values)!=vectors[kind] or not all(re.fullmatch(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?',v) for v in values):raise DeclarationError('Vector defaults require an explicit B.Type.new(...) with literal coordinates')
                if not all(math.isfinite(float(v)) for v in values):raise DeclarationError('Vector PROPERTY coordinates must be finite')
                default=','.join(values)
            else:raise DeclarationError('Unsupported declarative Lua property type: '+kind)
            properties.append((tokens[i-4],kind,tokens[i-2],default,rules))
    if not components:
        if properties:raise DeclarationError('PROPERTY requires a COMPONENT declaration')
        return None
    if len(components)!=1:raise DeclarationError('Declare exactly one Lua COMPONENT')
    variable,name=components[0]
    if any(owner!=variable for owner,_,_,_,_ in properties):raise DeclarationError('PROPERTY must belong to the declared component')
    return name,[(kind,field,default,(),rules) for _,kind,field,default,rules in properties]
