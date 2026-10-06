"""Extract the engine's own public declarations for documentation coverage.

This deliberately scans this repository's declaration style, not arbitrary C++.
Function bodies and Detail namespaces are skipped. The checked-in snapshot is
reviewed alongside curated descriptions, rather than publishing implementation.
"""
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\[\[[\s\S]*?\]\]|[A-Za-z_]\w*|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[fuUlL]*|::|&&|\.\.\.|==|!=|<=|>=|\+=|-=|\*=|/=|[^\s]')


def tokens(source):
    source = re.sub(r'(?m)^#define BAZZALT_MATERIAL_BUILDER_SET[^\n]*\n(?:[^\n]*\\\n)*[^\n]*\n', '', source)
    source = re.sub(r'BAZZALT_MATERIAL_BUILDER_SET\((\w+),([^\)]+)\)', r'MaterialBuilder& \1(std::string name, \2 value);', source)
    source = re.sub(r'^\s*#.*$', '', source, flags=re.M)
    return [t for t in TOKEN.findall(source) if not t.startswith(('//', '/*', '[[')) and t != 'BAZZALT_API']


def text(seq):
    value = ' '.join(seq)
    for before, after in [(' :: ', '::'), (' ( ', '('), (' )', ')'), (' ;', ';'), (' ,', ','), (' < ', '<'), (' >', '>'), (' { ', '{'), (' }', '}')]:
        value = value.replace(before, after)
    return value.strip()


def end_pair(ts, start, left='{', right='}'):
    depth = 0
    for index in range(start, len(ts)):
        if ts[index] == left: depth += 1
        elif ts[index] == right:
            depth -= 1
            if not depth: return index
    raise ValueError(f'Unbalanced declaration near {text(ts[start:start+12])}')


def inventory(root=REPO / 'include/Bazzalt'):
    found = []
    for header in sorted(root.rglob('*.h')):
        if 'Detail' in header.parts: continue
        ts = tokens(header.read_text(encoding='utf-8'))
        _scope(ts, found, header.relative_to(root).as_posix(), '', False)
    found = [x for x in found if not x['name'].startswith(('type_hash', 'std'))]
    math = next(x for x in found if x['name'] == 'MathFunctions')
    math['members'] += [dict(name=n,kind='field',signature=s) for n,s in [('Pi','inline constexpr float Pi = 3.14159265358979323846f'),('Epsilon','inline constexpr float Epsilon = 1.0e-6f')]]
    math['members'] += [dict(name=n,kind='alias',signature='using '+n+' = '+target) for n,target in [('Vector2','Vec2'),('Vector3','Vec3'),('Vector4','Vec4'),('Matrix3','Mat3'),('Matrix4','Mat4')]]
    return list({(x['name'], x['header']): x for x in found}.values())


def _scope(ts, found, header, owner, internal):
    i = 0
    while i < len(ts):
        if ts[i] == 'namespace':
            j = i + 1
            while j < len(ts) and ts[j] not in ('{', ';'): j += 1
            if j < len(ts) and ts[j] == '{':
                finish = end_pair(ts, j)
                _scope(ts[j+1:finish], found, header, owner, internal or 'Detail' in ts[i+1:j] or 'Runtime' in ts[i+1:j])
                i = finish + 1;continue
        if ts[i] in ('class', 'struct', 'enum') and not internal:
            kind = ts[i];j = i + 1
            if kind == 'enum' and j < len(ts) and ts[j] == 'class': j += 1
            if j >= len(ts): break
            name = ts[j];j += 1
            # Ignore forward declarations and references such as friend class.
            if i and ts[i-1] in ('friend', 'typename'): i += 1;continue
            if j < len(ts) and ts[j] == '<':
                end = end_pair(ts, j, '<', '>')
                name += '<' + ''.join(ts[j+1:end]) + '>'
                j = end + 1
            while j < len(ts) and ts[j] not in ('{', ';'): j += 1
            if j < len(ts) and ts[j] == '{':
                finish = end_pair(ts, j)
                qualified = (owner + '::' if owner else '') + name
                begin=i
                if i and ts[i-1]=='>':
                    depth=0
                    for back in range(i-1,-1,-1):
                        if ts[back]=='>': depth+=1
                        elif ts[back]=='<':
                            depth-=1
                            if depth==0:
                                if back and ts[back-1]=='template': begin=back-1
                                break
                entry = dict(name=qualified, header=header, kind=kind, declaration=text(ts[begin:j]), members=[])
                if kind == 'enum':
                    for part in split(ts[j+1:finish], ','):
                        if part: entry['members'].append(dict(name=part[0], kind='value', signature=text(part)))
                else:
                    _members(ts[j+1:finish], entry, found, kind == 'struct')
                found.append(entry);i = finish + 1;continue
        if header == 'Math.h' and ts[i] in ('constexpr', 'inline') and not internal:
            j = i
            while j < len(ts) and ts[j] not in ('{', ';'): j += 1
            seq = ts[i:j]
            opening = method_open(seq)
            if opening is not None and '::' not in seq[:opening] and j < len(ts) and ts[j] == '{':
                entry = next((x for x in found if x['name'] == 'MathFunctions'), None)
                if not entry:
                    entry = dict(name='MathFunctions', header=header, kind='namespace', declaration='namespace Bazzalt', members=[]);found.append(entry)
                _declaration(seq, entry, 'public');i = end_pair(ts, j)+1;continue
            if j < len(ts) and ts[j] == '{': i = end_pair(ts, j)+1;continue
            i = j+1;continue
        i += 1


def split(ts, delimiter):
    parts = [];current = [];depth = 0
    for token in ts:
        if token in ('(', '{', '[', '<'): depth += 1
        elif token in (')', '}', ']', '>'): depth -= 1
        if token == delimiter and depth == 0:
            parts.append(current);current = []
        else: current.append(token)
    parts.append(current);return parts


def _members(ts, entry, found, public):
    i = 0;start = 0;visibility = 'public' if public else 'private'
    while i < len(ts):
        if ts[i] in ('public', 'private', 'protected') and i+1 < len(ts) and ts[i+1] == ':':
            visibility = ts[i];i += 2;start = i;continue
        if ts[i] == '{':
            finish = end_pair(ts, i)
            prefix = ts[start:i]
            if 'struct' in prefix or 'class' in prefix or 'enum' in prefix:
                if visibility != 'private': _scope(ts[start:finish+1], found, entry['header'], entry['name'], False)
                # Nested data instance after its type, e.g. } DepthOfField{};
                if finish+1 < len(ts) and re.match(r'^\w+$', ts[finish+1]):
                    nested_name = prefix[prefix.index('struct')+1] if 'struct' in prefix else 'nested'
                    entry['members'].append(dict(name=ts[finish+1],kind='field',signature=nested_name+' '+ts[finish+1]+'{}'))
                    while finish < len(ts)-1 and ts[finish+1] != ';': finish += 1
                    if finish < len(ts)-1: finish += 1
                i = finish+1;start=i;continue
            # A body follows a method signature; initializer braces are retained.
            opening = method_open(prefix)
            if opening is not None and ')' in prefix[opening:] and not ('using' in prefix):
                if visibility != 'private': _declaration(prefix, entry, visibility)
                i = finish+1
                if i < len(ts) and ts[i] == ';': i += 1
                start = i;continue
            i = finish+1;continue
        if ts[i] == ';':
            if visibility != 'private': _declaration(ts[start:i], entry, visibility)
            i += 1;start=i;continue
        i += 1


def _declaration(seq, entry, visibility):
    if not seq or seq[0] in ('static_assert', '#'): return
    if seq[0] == 'friend' and '(' not in seq: return
    template=''
    if seq[0] == 'template' and '<' in seq:
        end = end_pair(seq, seq.index('<'), '<', '>');template=text(seq[:end+1])+' ';seq = seq[end+1:]
    if not seq: return
    signature = template+text(seq)
    pointer = re.search(r'\(\s*\*\s*(\w+)\s*\)', signature)
    if pointer:
        entry['members'].append(dict(name=pointer[1], kind='field', signature=signature));return
    opening = method_open(seq)
    if opening is not None:
        before = seq
        if 'operator' in before:
            name = 'operator' + ''.join(before[before.index('operator')+1:opening])
            if name == 'operator' and opening+1 < len(seq) and seq[opening+1] == ')': name = 'operator()'
        else:
            name = before[opening-1]
            if opening > 1 and before[opening-2] == '~': name = '~' + name
        if name in ('static_assert',): return
        entry['members'].append(dict(name=name,kind='method',signature=signature,access=visibility))
    elif seq[0] == 'using':
        entry['members'].append(dict(name=seq[1],kind='alias',signature=signature))
    else:
        for field in split(seq, ','):
            before = field[:field.index('=')] if '=' in field else field
            if '{' in before: before = before[:before.index('{')]
            if '[' in before: before = before[:before.index('[')]
            if not before: continue
            name = before[-1]
            if re.fullmatch(r'[A-Za-z_]\w*', name):
                entry['members'].append(dict(name=name,kind='field',signature=signature))


def method_open(seq):
    depth = 0
    for index, token in enumerate(seq):
        if token == '<' and (not index or seq[index-1] != 'operator'): depth += 1
        elif token == '>': depth -= 1
        elif token == '=' and depth == 0 and (not index or seq[index-1] != 'operator'): return None
        elif token == '(' and depth == 0:
            # Function-pointer data members belong to fields, not methods.
            if index + 1 < len(seq) and seq[index+1] == '*': return None
            return index
    return None


def lua_inventory():
    """Enumerate registrations, including wrapper-only names and binding macros."""
    types = {}
    for path in sorted((REPO / 'src/Script').glob('Lua*.cpp')):
        source = path.read_text(encoding='utf-8')
        source = re.sub(r'^#define.*$', '', source, flags=re.M)
        for macro, variable, owner in [('BUILDER','builder','MaterialBuilder'),('STATE','state','MaterialRenderState'),('DEPTH','depth','MaterialDepthFunction')]:
            source = re.sub(r'BAZZALT_LUA_'+macro+r'\((\w+)\)', lambda m: f'{variable}["{m[1]}"]=&{owner}::{m[1]};', source)
        variables = {}
        pattern = re.compile(r'(?:auto\s+|sol::usertype<[^>]+>\s+)(\w+)\s*=\s*api\.(new_usertype<([^>]+)>|create_named)\("([^"]+)"|sol::usertype<([^>]+)>\s+(\w+)\s*=\s*api\["([^"]+)"\]|(\w+)\["([^"]+)"\]\s*=')
        for match in pattern.finditer(source):
            if match[1]:
                variable, cpp, name = match[1], match[3] or match[4], match[4]
                variables[variable] = name
                types.setdefault(name, dict(name=name, cpp='CameraPostProcessing::DepthOfFieldSettings' if cpp == 'DoF' else cpp, members=set(), constructor=False))
                if match[3]:
                    tail=source[match.end():source.find(';',match.end())]
                    types[name]['constructor'] |= 'no_constructor' not in tail
            elif match[5]:
                variables[match[6]] = match[7]
                types.setdefault(match[7],dict(name=match[7],cpp=match[5],members=set(),constructor=True))
            elif match[8] in variables:
                name = variables[match[8]]
                if name in types: types[name]['members'].add(match[9])
    # Registrations implemented by generic helper templates and dynamic dispatch.
    for name, axes in [('Vec2','XY'),('Vec3','XYZ'),('Vec4','XYZW')]:
        types.setdefault(name, dict(name=name,cpp=name,members=set(),constructor=True))
        types[name]['members'].update(list(axes)+['Length','LengthSquared','Normalized','Dot'])
    for name in ('Mat3','Mat4'):
        types.setdefault(name, dict(name=name,cpp=name,members=set(),constructor=True))
        types[name]['members'].update(['Identity','Transposed','Inversed','Get','Set'])
    types['Entity']['members'].update(['HasComponent','GetComponent','AddComponent','RemoveComponent','IsComponentEnabled','SetComponentEnabled','SetComponent'])
    types['MathFunctions']=dict(name='MathFunctions',cpp='MathFunctions',members=['Pi','Epsilon','ToRadians','ToDegrees','Clamp','Lerp','IsNearlyEqual'],constructor=False)
    # These are private dispatch tables, not supported gameplay APIs.
    types.pop('_ComponentOperations', None)
    return [dict(t, members=sorted(t['members'])) for t in types.values()]


if __name__ == '__main__':
    for type_ in inventory():
        print(type_['name'] + ': ' + ', '.join(m['name'] for m in type_['members']))
