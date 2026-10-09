"""Inspector-only, bounded expressions. No eval, imports, or project execution."""
import ast
import fnmatch
import math
import re

class RuleError(ValueError):pass

def _Finite(value):
    try:return math.isfinite(value)
    except (TypeError,OverflowError):return False

def _Tree(expression):
    if not isinstance(expression,str) or len(expression)>512:raise RuleError('Inspector expression must be a string of at most 512 characters')
    # Translate Lua literals/operators outside quoted strings only.
    parts=re.split(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')',expression)
    for i in range(0,len(parts),2):
        parts[i]=parts[i].replace('~=','!=')
        for old,new in (('true','True'),('false','False'),('nil','None')):parts[i]=re.sub(r'\b'+old+r'\b',new,parts[i])
    try:tree=ast.parse(''.join(parts).strip(),mode='eval')
    except SyntaxError as error:raise RuleError('Invalid inspector expression') from error
    nodes=list(ast.walk(tree))
    allowed=(ast.Expression,ast.Constant,ast.Name,ast.Load,ast.Attribute,ast.BoolOp,ast.And,ast.Or,ast.UnaryOp,ast.Not,ast.USub,ast.UAdd,ast.BinOp,ast.Add,ast.Sub,ast.Mult,ast.Div,ast.Mod,ast.Compare,ast.Eq,ast.NotEq,ast.Lt,ast.LtE,ast.Gt,ast.GtE,ast.Call)
    if len(nodes)>100 or any(not isinstance(node,allowed) for node in nodes):raise RuleError('Unsupported inspector expression')
    for node in nodes:
        if isinstance(node,ast.Name) and node.id not in ('value','self','Len','Contains','StartsWith','EndsWith','Matches','Abs','Min','Max'):raise RuleError('Unknown inspector expression name: '+node.id)
        if isinstance(node,ast.Attribute) and (not isinstance(node.value,ast.Name) or node.value.id!='self' or node.attr.startswith('_')):raise RuleError('Only self.Field references are allowed')
        if isinstance(node,ast.Call) and (not isinstance(node.func,ast.Name) or node.func.id not in ('Len','Contains','StartsWith','EndsWith','Matches','Abs','Min','Max') or node.keywords or len(node.args)>3):raise RuleError('Unsupported inspector function')
        if isinstance(node,ast.Constant) and (not isinstance(node.value,(str,int,float,bool,type(None))) or isinstance(node.value,str) and len(node.value)>256):raise RuleError('Invalid inspector expression literal')
    return tree.body

def Evaluate(expression,properties,value=None):
    tree=_Tree(expression)
    def run(node):
        if isinstance(node,ast.Constant):return node.value
        if isinstance(node,ast.Name):
            if node.id=='value':return value
            raise RuleError('Use self.Field to read other properties')
        if isinstance(node,ast.Attribute):
            if node.attr not in properties:raise RuleError('Missing inspector dependency: '+node.attr)
            return properties[node.attr]
        if isinstance(node,ast.BoolOp):
            result=False
            for part in node.values:
                result=bool(run(part))
                if isinstance(node.op,ast.And) and not result or isinstance(node.op,ast.Or) and result:return result
            return result
        if isinstance(node,ast.UnaryOp):
            v=run(node.operand)
            if isinstance(node.op,ast.Not):return not v
            if isinstance(v,bool) or not isinstance(v,(int,float)):raise RuleError('Expected a number')
            return -v if isinstance(node.op,ast.USub) else v
        if isinstance(node,ast.BinOp):
            a,b=run(node.left),run(node.right)
            if any(isinstance(v,bool) or not isinstance(v,(int,float)) for v in (a,b)):raise RuleError('Arithmetic requires numbers')
            operations={ast.Add:lambda:a+b,ast.Sub:lambda:a-b,ast.Mult:lambda:a*b,ast.Div:lambda:a/b,ast.Mod:lambda:a%b}
            v=operations[type(node.op)]()
            if not math.isfinite(v):raise RuleError('Non-finite inspector expression')
            return v
        if isinstance(node,ast.Compare):
            a=run(node.left)
            for op,nextNode in zip(node.ops,node.comparators):
                b=run(nextNode);tests={ast.Eq:lambda:a==b,ast.NotEq:lambda:a!=b,ast.Lt:lambda:a<b,ast.LtE:lambda:a<=b,ast.Gt:lambda:a>b,ast.GtE:lambda:a>=b}
                if not tests[type(op)]():return False
                a=b
            return True
        if isinstance(node,ast.Call):
            args=[run(n) for n in node.args]
            if any(isinstance(v,str) and len(v)>4096 for v in args):raise RuleError('Inspector string exceeds expression limit')
            functions={'Len':len,'Contains':lambda s,p:p in s,'StartsWith':lambda s,p:s.startswith(p),'EndsWith':lambda s,p:s.endswith(p),'Matches':lambda s,p:fnmatch.fnmatchcase(s,p),'Abs':abs,'Min':min,'Max':max}
            return functions[node.func.id](*args)
        raise RuleError('Unsupported inspector expression')
    try:return run(tree)
    except (TypeError,ValueError,ZeroDivisionError,OverflowError,AttributeError) as error:raise RuleError(str(error)) from error

def CheckRules(rules):
    known={'Min','Max','Step','MaxLength','MinLength','Trim','Filter','Pattern','ReadOnly','VisibleIf','EnabledIf','Validate','Message','Tooltip','Label'}
    if not isinstance(rules,dict) or set(rules)-known:raise RuleError('Unknown inspector property rule')
    for key,value in rules.items():
        if key in ('Min','Max','Step','MaxLength','MinLength'):
            if isinstance(value,bool) or not isinstance(value,(int,float)) or not _Finite(value):raise RuleError(key+' must be finite')
            if key in ('MaxLength','MinLength') and (value<0 or value>4096 or int(value)!=value):raise RuleError('String length must be 0–4096')
            if key=='Step' and value<=0:raise RuleError('Step must be positive')
        elif key in ('ReadOnly','Trim'):
            if not isinstance(value,bool):raise RuleError(key+' must be boolean')
        else:
            if not isinstance(value,str) or len(value)>512:raise RuleError(key+' must be a short string')
            if key in ('VisibleIf','EnabledIf','Validate'):_Tree(value)
            if key=='Filter' and value not in ('Digits','Letters','Alphanumeric','Identifier'):raise RuleError('Unknown string filter')
    if rules.get('Min',-math.inf)>rules.get('Max',math.inf) or rules.get('MinLength',0)>rules.get('MaxLength',4096):raise RuleError('Minimum exceeds maximum')
    return rules

def CheckTypeRules(kind,rules):
    CheckRules(rules);kind=kind.replace('Bazzalt::','').strip()
    if set(rules)&{'Min','Max','Step'} and kind not in ('float','double','int','Vec2','Vec3','Vec4','Quaternion'):raise RuleError('Numeric rules require a numeric property')
    if set(rules)&{'MinLength','MaxLength','Trim','Filter','Pattern'} and kind not in ('string','std::string'):raise RuleError('String rules require a string property')
    if kind=='int' and any(int(rules[k])!=rules[k] for k in ('Min','Max','Step') if k in rules):raise RuleError('Integer bounds and steps must be integers')
    if kind=='int' and (any(not -2147483648<=rules[k]<=2147483647 for k in ('Min','Max') if k in rules) or rules.get('Step',1)>2147483647):raise RuleError('Integer rules exceed the supported range')
    return rules

def State(rules,properties):
    visible=bool(Evaluate(rules['VisibleIf'],properties)) if 'VisibleIf' in rules else True
    enabled=not rules.get('ReadOnly',False) and (bool(Evaluate(rules['EnabledIf'],properties)) if 'EnabledIf' in rules else True)
    return visible,enabled

def Validate(rules,properties,value):
    if isinstance(value,str):
        if rules.get('Trim'):value=value.strip()
        if not rules.get('MinLength',0)<=len(value)<=rules.get('MaxLength',4096):raise RuleError('String length is outside the allowed range')
        filters={'Digits':str.isdecimal,'Letters':str.isalpha,'Alphanumeric':str.isalnum,'Identifier':lambda v:bool(re.fullmatch(r'[A-Za-z_]\w*',v))}
        if 'Filter' in rules and value and not filters[rules['Filter']](value):raise RuleError('String does not match the filter')
        if 'Pattern' in rules and not fnmatch.fnmatchcase(value,rules['Pattern']):raise RuleError('String does not match the pattern')
    for number in value if isinstance(value,(tuple,list)) else (value,):
        if isinstance(number,(int,float)) and not isinstance(number,bool):
            if not _Finite(number) or not rules.get('Min',-math.inf)<=number<=rules.get('Max',math.inf):raise RuleError('Number is outside the allowed range')
    candidate=dict(properties);candidate['value']=value
    if 'Validate' in rules and not Evaluate(rules['Validate'],candidate,value):raise RuleError(rules.get('Message','Value does not satisfy the inspector rule'))
    return value
