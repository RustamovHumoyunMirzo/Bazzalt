"""Keep the current static API reference complete without copying source by hand.

Run before build_public_docs.py. Only marked sections are regenerated; prose outside
them stays editable. Historical versions are never rewritten. No third-party parser
or online service is needed. This is a declaration/binding inventory, not a C++ ABI
parser; the exact public header contracts are retained alongside the tutorials.
"""
from __future__ import annotations
import argparse
import html
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- API-SOURCE-BEGIN -->'
END = '<!-- API-SOURCE-END -->'
EXCLUDED = {'Export.h', 'EcsConfig.h'}


def masked(source):
    """Keep offsets while hiding comments and string contents from brace scanning."""
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ' ' * len(m[0]), source)


def closing(text, start):
    depth = 1
    for i in range(start + 1, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if not depth:
            return i
    raise ValueError('Unbalanced source block')


def public_contract(source, kind, start, end):
    """Remove private/protected sections and inline function implementations."""
    body = source[start + 1:end]
    scan = masked(body)
    visible = kind != 'class'
    parts = []
    begin = i = 0
    while i < len(scan):
        access = re.match(r'\s*(public|private|protected)\s*:', scan[i:]) if i == begin else None
        if access:
            visible = access[1] == 'public'
            i += access.end(); begin = i
            continue
        if scan[i] == '{':
            stop = closing(scan, i)
            prefix = body[begin:i].strip()
            # A function body has a closing parameter list; a field initializer
            # does not. Nested type definitions remain intact.
            if ')' in masked(prefix) and not re.match(r'\s*(?:template\s*<[^;]+>\s*)?(?:class|struct|enum\s+class)\s+\w+', masked(prefix)):
                if visible:
                    prefix = re.sub(r'\s*:\s*[A-Za-z_]\w*\s*\([\s\S]*$', '', prefix)
                    parts.append(prefix + ';')
                i = stop + 1
                if i < len(scan) and scan[i] == ';': i += 1
                begin = i
                continue
            i = stop + 1
            continue
        if scan[i] == ';':
            declaration = body[begin:i + 1].strip()
            if visible and declaration and not declaration.startswith('friend '):
                parts.append(declaration)
            begin = i + 1
        i += 1
    if kind.startswith('enum'):
        return body.strip()
    return '\n'.join(parts)


def cpp_types():
    result = {}
    pattern = re.compile(r'\b(enum\s+class|struct|class)\s+(?:BAZZALT_API\s+)?(\w+)\b([^;{}()<>]*?)\{')
    for path in sorted((ROOT / 'include/Bazzalt').rglob('*.h')):
        if 'Detail' in path.parts or path.name in EXCLUDED: continue
        source = path.read_text(encoding='utf-8'); scan = masked(source)
        internal = [(m.end()-1, closing(scan,m.end()-1)) for m in re.finditer(r'namespace\s+Detail\s*\{',scan)]
        for match in pattern.finditer(scan):
            kind, name = match[1], match[2]
            if any(a < match.start() < b for a,b in internal): continue
            if name in ('TimeState', 'InputState'): continue
            start = match.end() - 1; end = closing(scan, start)
            contract = public_contract(source, kind, start, end)
            # Protected GetScene is intentionally available to subsystem authors.
            if name == 'Subsystem': contract += '\nprotected:\n    static Scene& GetScene();'
            if name == 'Application':
                contract = re.sub(r'#define BAZZALT_APP_[\s\S]*', '', contract).strip()
                for category, args in (('SIGNAL', ''), ('TIMED_SIGNAL', 'float'), ('SCENE_SIGNAL', 'const std::string&')):
                    for m in re.finditer(r'(?m)^\s*BAZZALT_APP_' + category + r'\((On\w+)', source):
                        contract += f'\nstatic Subscription {m[1]}(std::function<void({args})> callback);'
                contract += '\nstatic Subscription OnWindowResize(std::function<void(std::uint32_t,std::uint32_t)> callback);\nstatic Subscription OnWindowFocusChanged(std::function<void(bool)> callback);'
            # Partial template specializations have their own existing reference.
            if name == 'Descriptor': name = 'ComponentSerializationRegistry-Descriptor'
            if name == 'DepthOfFieldSettings': name = 'CameraPostProcessing-DepthOfFieldSettings'
            result.setdefault(name, (path.relative_to(ROOT / 'include').as_posix(), kind, contract))
    return result


def lua_types():
    """Inventory real usertype/enum/table registrations, not all public C++ types.

    Binding blocks retain lambda adapters: these are important because Lua may
    return tuples/copies where C++ uses output parameters or references.
    """
    result = {}
    pattern = re.compile(r'(?:(?:api\.new_usertype<[^\n;]+?>|api\.new_enum|api\.create_named|Type<\w+>)\(\s*(?:api,\s*)?"([A-Za-z]\w*)")')
    for path in sorted((ROOT / 'src/Script').glob('Lua*.cpp')):
        source = path.read_text(encoding='utf-8')
        scan = masked(source)
        matches = list(pattern.finditer(source))
        owners = []
        def statement_end(start):
            depth = 0
            for i in range(start,len(scan)):
                depth += (scan[i] in '({[') - (scan[i] in ')}]')
                if scan[i] == ';' and depth == 0: return i+1
            return len(scan)
        for match in matches:
            name = match[1]
            if name.startswith('_') or name == 'ApplicationEvents': continue
            prefix = source[max(0,match.start()-100):match.start()]
            variable = re.search(r'(\w+)\s*=\s*$',prefix)
            if variable: owners.append((match.start(), variable[1],name))
            entry = source[match.start():statement_end(match.start())]
            result[name] = (path.relative_to(ROOT).as_posix(), {'construction':entry})
        # Also handle aliases used for vector/matrix extension bindings.
        for m in re.finditer(r'\b(\w+)\s*=\s*api\["(\w+)"\]',source):
            if m[2] in ('Vec2','Vec3','Vec4','Mat3','Mat4'): owners.append((m.start(),m[1],m[2]))
        owners.sort()
        for m in re.finditer(r'\b(\w+)\["(\w+)"\]\s*=',source):
            previous = [v for v in owners if v[0]<m.start() and v[1]==m[1]]
            if not previous: continue
            name = previous[-1][2]
            if name not in result: result[name]=(path.relative_to(ROOT).as_posix(),{})
            result[name][1][m[2]] = source[m.end():statement_end(m.end())].strip().rstrip(';')
        if path.name == 'LuaAudio.cpp':
            for m in re.finditer(r'AUDIO_FIELD\(AudioBusSettings,(\w+)\)',source):
                result['AudioBusSettings'][1][m[1]]='&AudioBusSettings::'+m[1]
            for m in re.finditer(r'AUDIO_METHOD\((\w+)\)',source):
                if m[1]!='F': result['AudioVoice'][1][m[1]]='&AudioVoice::'+m[1]
        if path.name == 'LuaApplicationEvents.cpp':
            for m in re.finditer(r'APP_GET\((\w+)\)',source):
                if m[1]!='Name': result['Application'][1][m[1]]='&Application::'+m[1]
    # Shared template registrations are instantiated with these exact names.
    math = (ROOT/'src/Script/LuaMath.cpp').read_text(encoding='utf-8')
    for name in ('Vec2','Vec3','Vec4','Mat3','Mat4'):
        if name not in result: result[name] = ('src/Script/LuaMath.cpp', {})
        template = math[math.index('template<class T>'):math.index('template<class T,int N>')] if name.startswith('Vec') else math[math.index('template<class T,int N>'):math.index('void BindLuaMath')]
        for m in re.finditer(r'type\["(\w+)"\]\s*=([^;]+);',template):
            result[name][1][m[1]]=m[2].replace('T::',name+'::').replace('T value',name+' value')
    result['MathFunctions']=('src/Script/LuaMath.cpp', {m[1]:m[2] for m in re.finditer(r'api\["(\w+)"\]\s*=([^;]+);',math)})
    # Physics uses one field schema for serialization, editor authoring and Lua.
    physics=(ROOT/'src/Script/LuaPhysics.cpp').read_text(encoding='utf-8')
    schema=(ROOT/'src/Physics/PhysicsFields.h').read_text(encoding='utf-8').replace('\\\n',' ')
    macros={m[1]:m[2] for m in re.finditer(r'#define (BAZZALT_\w+_FIELDS)\(X\) ([^\n]+)',schema)}
    def fields(macro):
        text=macros[macro]
        for child in re.findall(r'(BAZZALT_\w+_FIELDS)\(X\)',text):text=text.replace(child+'(X)',macros[child])
        return re.findall(r'X\((\w+),',text)
    for name,macro in (('Rigidbody','BAZZALT_BODY_FIELDS'),('ShapeCollider','BAZZALT_SHAPE_FIELDS'),('MeshCollider','BAZZALT_MESH_COLLIDER_FIELDS'),('PhysicsSettings','BAZZALT_PHYSICS_SETTINGS_FIELDS'),('Joint','BAZZALT_JOINT_FIELDS'),('DistanceJoint','BAZZALT_JOINT_FIELDS'),('HingeJoint','BAZZALT_JOINT_FIELDS'),('SliderJoint','BAZZALT_JOINT_FIELDS'),('BallAndSocketJoint','BAZZALT_JOINT_FIELDS'),('FixedJoint','BAZZALT_JOINT_FIELDS'),('Cloth','BAZZALT_CLOTH_FIELDS'),('Rope','BAZZALT_ROPE_FIELDS')):
        for field in fields(macro):result[name][1].setdefault(field,'&'+name+'::'+field)
    for variable,name in (('hit','PhysicsHit'),('event','CollisionEvent')):
        start=physics.index('#define FIELD(Name) '+variable);end=physics.index('#undef FIELD',start)
        for field in re.findall(r'FIELD\((\w+)\)',physics[start:end]):
            if field!='Name':result[name][1].setdefault(field,'&'+name+'::'+field)
    for method in re.findall(r'METHOD\((\w+)\)',physics):
        if method!='Name':result['PhysicsScene'][1].setdefault(method,'&PhysicsScene::'+method)
    # Scene physics overloads are bound after the base Scene type at runtime.
    start=physics.index('sol::usertype<Scene> scene=api["Scene"]')
    for match in re.finditer(r'scene\["(\w+)"\]\s*=',physics[start:]):
        begin=start+match.end();scan=masked(physics);depth=0;end=begin
        for end in range(begin,len(scan)):
            depth+=(scan[end] in '({[')-(scan[end] in ')}]')
            if scan[end]==';' and depth==0:break
        result['Scene'][1][match[1]]=physics[begin:end].strip()

    return result


def section(title, text):
    return f'<h2 id="source-contract">{title}</h2>\n{text}'


def lua_type(value):
    value = re.sub(r'\b(const|static|constexpr|virtual|BAZZALT_API)\b|\[\[.*?\]\]|[&*]', '', value).strip()
    value = value.replace('std::string','string').replace('std::filesystem::path','string')
    value = value.replace('sol::protected_function','function').replace('sol::function','function')
    value = re.sub(r'\b(?:std::)?(?:u?int\d+_t|size_t|float|double|int)\b','number',value)
    value = re.sub(r'\bbool\b','boolean',value)
    value = re.sub(r'(?:std::|sol::)(?:optional|as_table_t|vector)<(.+)>',r'\1',value)
    return value.replace('Bazzalt::','').strip()


def split_arguments(source):
    depth = 0; start = 0; parts = []
    for i,c in enumerate(source):
        depth += (c in '<({[') - (c in '>)}]')
        if c == ',' and depth == 0: parts.append(source[start:i]); start=i+1
    if source[start:].strip(): parts.append(source[start:])
    return parts


def lua_usage(name, member, binding, signatures):
    """Begin with Lua notation; retain the adapter only as optional detail."""
    graphics_signatures = {
        'CreateMesh': 'vertices: GraphicsVertex[], indices: number[] (optional), topology: GraphicsTopology (optional)) -> GraphicsMesh',
        'DrawMesh': 'mesh: GraphicsMesh, material: Material (optional), transform: Mat4 (optional), options: GraphicsDrawOptions (optional))',
        'DrawVertices': 'vertices: GraphicsVertex[], indices: number[] (optional), topology: GraphicsTopology (optional), material: Material (optional), transform: Mat4 (optional), options: GraphicsDrawOptions (optional))',
        'DrawLine': 'from: Vec3, to: Vec3, color: Vec4 (optional), options: GraphicsDrawOptions (optional))',
        'DrawTriangle': 'a: Vec3, b: Vec3, c: Vec3, color: Vec4 (optional), options: GraphicsDrawOptions (optional))',
    }
    extension_signatures = {
        'Call': 'moduleName: string, functionName: string, arguments: table (optional)) -> value',
        'GetAsset': 'extensionId: string, assetId: string) -> UUID',
        'HasModule': 'name: string) -> boolean',
        'GetModules': ') -> string[]',
    }
    if name == 'Extensions' and member in extension_signatures:
        return '<pre><code class="language-lua">'+html.escape('Bazzalt.Extensions.'+member+'('+extension_signatures[member])+'</code></pre>'
    if name == 'Graphics' and member in graphics_signatures:
        return '<pre><code class="language-lua">'+html.escape('Bazzalt.Graphics.'+member+'('+graphics_signatures[member])+'</code></pre>'
    if member == 'construction':
        if 'create_named' in binding: return '<p>Global service table; do not construct an instance.</p>'
        if 'new_enum' in binding:
            values = re.findall(r'"(\w+)"\s*,\s*\w+::\w+', binding)
            return '<p>Enum values: '+', '.join('<code>Bazzalt.'+name+'.'+v+'</code>' for v in values)+'.</p>'
        if 'no_constructor' in binding: return '<p>Runtime-owned value; there is no public Lua constructor.</p>'
        return '<p>Construct a value with <code>Bazzalt.'+name+'()</code>. Constructor overloads, when available, are shown in the binding details.</p>'
    clean = [re.sub(r'//[^\n]*|/\*[\s\S]*?\*/','',s).strip() for s in signatures]
    declaration = clean[0] if clean else ''
    field = binding.startswith('&') and declaration and not re.search(r'\b'+re.escape(member)+r'\s*\(',declaration)
    if field:
        match = re.search(r'(.+?)\b'+re.escape(member)+r'\b',declaration,re.S)
        # Multiple fields may share a declaration: bool A=false,B=false.
        # B's type is bool, not the preceding declarator/initializer text.
        leading = re.match(r'([\w:]+(?:<[^;]+>)?)\s+',declaration)
        field_type = leading[1] if leading else match[1] if match else 'value'
        return '<pre><code>'+html.escape('value.'+member+': '+lua_type(field_type))+'</code></pre><p>Read/write value field. Defaults are shown in the declaration below. For copied entity components, commit edits with <code>entity:SetComponent(typeName, value)</code>.</p>'
    direct = re.search(r'\b'+re.escape(member)+r'\s*\((.*?)\)\s*(?:const|noexcept|;|$)',declaration,re.S)
    adapter = re.search(r'\]\s*\(([^)]*)\)',binding)
    args = split_arguments(adapter[1] if adapter else direct[1] if direct else '')
    params=[]
    for index,arg in enumerate(args):
        arg=arg.strip()
        if index==0 and adapter and (re.match(r'(?:const\s+)?'+re.escape(name)+r'[&\s]',arg) or arg=='sol::table'): continue
        if 'sol::this_state' in arg: continue
        default = arg.partition('=')[2].strip()
        raw=arg.partition('=')[0].strip()
        match=re.match(r'(.+?[\s&*])([A-Za-z_]\w*)$',raw)
        typ,identifier=(match[1],match[2]) if match else (raw,'argument'+str(index+1))
        optional='optional<' in typ
        params.append(identifier+': '+lua_type(typ)+(' = '+default if default else ' (optional)' if optional else ''))
    service = 'static ' in declaration or name in ('Application','GUI','Audio','AudioMixer','Console','SceneManager','AssetManager','Time','Input','MathFunctions')
    call = ('Bazzalt.'+name+'.' if service else 'value:')+member+'('+', '.join(params)+')'
    return '<pre><code class="language-lua">'+html.escape(call)+'</code></pre>'


def supplement(language, name, record, types):
    if language == 'native':
        header, kind, contract = record
        display_name = name.replace('-', '::')
        return section('Public declarations', f'<p>Header: <code>{header}</code>. Namespace: <code>Bazzalt</code>. '
            'Declarations below list public members, overloads, field types and default values. Inline implementation '
            'bodies and private storage are omitted. Inherited members are declared on the base type. '
            'A reference (<code>&amp;</code>) is not a value copy; check lifetime notes before retaining it.</p>\n'
            f'<pre><code class="language-cpp">{html.escape(kind+" "+display_name+" {\n"+contract+"\n};")}</code></pre>')
    source, entries = record
    body = ('<p>Use <code>Bazzalt.'+name+'</code>. Each member below is actually registered in Lua. '
        'The adapter declarations are C++ binding notation, not Lua example code. '
        '<code>sol::property</code> exposes a field, <code>sol::overload</code> lists alternatives, '
        '<code>sol::optional</code> accepts an omitted/nil argument, and <code>std::make_tuple</code> '
        'returns multiple Lua values. <code>sol::as_table</code> returns a Lua table. '
        'Instance methods use <code>value:Method(...)</code>; service/static calls use '
        '<code>Bazzalt.'+name+'.Method(...)</code> unless a colon overload is explicitly registered. '
        'An engine exception becomes a Lua script error. See the examples and behavior notes above for usage.</p>')
    contract = types.get(name, ('','',''))[2]
    for member, binding in sorted(entries.items()):
        # Direct native bindings have readable parameter/return declarations.
        signatures = [s.strip()+';' for s in contract.split(';') if re.search(r'\b'+re.escape(member)+r'\b',s)]
        value = '\n'.join(signatures) if signatures else binding
        body += f'<h3 id="lua-{member}">{member if member != "construction" else "Construction / enum values"}</h3>'
        body += lua_usage(name, member, binding, signatures)
        body += f'<details><summary>Native declaration and binding adapter</summary><pre><code>{html.escape(value)}</code></pre>'
        if signatures and not binding.startswith('&'): body += '<pre><code>'+html.escape(binding)+'</code></pre>'
        body += '</details>'
    body += f'<p>Binding source: <code>{source}</code>. Inherited component operations use the Entity component APIs.</p>'
    return section('Lua members and signatures',body)


OVERVIEWS = {
    'Joint': 'Configurable joint component and base of DistanceJoint, HingeJoint, SliderJoint, BallAndSocketJoint and FixedJoint. Inherited fields include ConnectedBody, anchors, axes, limits, motors, collision suppression and break thresholds. Angles use radians. Zero ConnectedBody means the world.',
    'DistanceJoint': 'Specialized Joint component. All public settings and IsValid are inherited from Joint; see the Joint reference for their declarations. Use Entity component access to configure it and PhysicsScene to break, repair or read its broken state.',
    'HingeJoint': 'Specialized Joint component. All public settings and IsValid are inherited from Joint; see the Joint reference for their declarations. Use Entity component access to configure it and PhysicsScene to break, repair or read its broken state.',
    'SliderJoint': 'Specialized Joint component. All public settings and IsValid are inherited from Joint; see the Joint reference for their declarations. Use Entity component access to configure it and PhysicsScene to break, repair or read its broken state.',
    'BallAndSocketJoint': 'Specialized Joint component. All public settings and IsValid are inherited from Joint; see the Joint reference for their declarations. Use Entity component access to configure it and PhysicsScene to break, repair or read its broken state.',
    'FixedJoint': 'Specialized Joint component. All public settings and IsValid are inherited from Joint; see the Joint reference for their declarations. Use Entity component access to configure it and PhysicsScene to break, repair or read its broken state.',
    'Cloth': 'Soft-body deformation of the attached PrimitiveObject or Mesh/model subtree. Original material slots and primary UV coordinates are preserved; no extra plane is generated. PinEdges bits select source-boundary left, right, top and bottom borders. Legacy Width/Height/SegmentsX/SegmentsY do not generate geometry. Compliance controls stretch/shear; BendMode selects optional None, Distance or Dihedral bending. SubdivisionLevel refines coarse source surfaces within the budget. Pressure inflates closed outward-wound meshes, and CollisionRadius controls contact thickness; Wind is world-space acceleration. MaterialAsset draws the deformed surface. Read and control zero-based vertices through PhysicsScene.',
    'Rope': 'Procedural soft-body tube extending down local Y. PinStart and PinEnd pin complete end rings. Wind is world-space acceleration. Mass, compliance, bending, damping, collision material and render material are independent settings.',
    'Application': 'Global application controls: frame limits, background behavior, session state, window dimensions, metadata and deferred quit. This service is not owned by an entity.',
    'Subsystem': 'Base class for a registered C++ project-wide service. It has no entity or Transform. Implement OnRegister to subscribe to global lifecycle events; OnUnregister is optional.',
    'Subsystems': 'Find loaded C++ subsystems by registered type name, or obtain a checked shared project interface. Returned pointers and references expire when the subsystem is unloaded.',
    'Scripts': 'Find a live native entity behavior or obtain its checked shared C++ interface. Instances may exist before OnCreate has run; references expire on script or scene teardown. This typed interface API is C++ only.',
    'GUI': 'Read GUI bounds and per-frame interaction state, control keyboard focus, and test whether UI consumes the pointer. Read interaction pulses during gameplay update.',
    'Frame': 'GUI root component. Only descendants render as its widgets: widgets attached to the Frame entity itself do not render. Supports viewport, camera-bound and spatial placement.',
    'RectTransform': 'GUI layout in top-left coordinates. Anchor fractions are relative to the parent; Position and Size are pixel offsets. Pivot determines the layout origin, not a 3D Transform pivot.',
    'GuiInteraction': 'Snapshot of a widget’s hover, press and focus states plus one-frame pointer/click/value/submit pulses. Poll during gameplay update rather than retaining pulses across frames.',
    'GuiImage': 'GUI image component. Use SetTexture for an imported/runtime texture asset, or SetRenderTexture for a camera capture; each setter clears the competing source.',
    'GuiTextInput': 'Editable UTF-8 GUI text with a placeholder, read-only/password modes and keyboard navigation. MaxLength counts Unicode code points, not bytes.',
    'AudioClip': 'Copyable UUID reference to an imported Audio Clip. Load constructs a reference, not playback. IsValid checks asset readiness; GetInfo returns metadata, or zeroed information on failure.',
    'AudioBus': 'Copyable mixer routing handle. UUID zero identifies the Master bus, not an absent bus. GetSettings returns a copy; apply changes with SetSettings or the individual setters.',
    'AudioMixer': 'Runtime mixer routing, effects and snapshots. Load replaces mixer configuration transactionally. Snapshot application changes mix state while retaining current routing.',
    'AudioVoice': 'Non-owning playback handle. Stopped, completed, replaced or stolen voices invalidate it. Time and duration use seconds; Seek is absolute. Entity-owned component values overwrite persistent voice volume/pitch edits at the next synchronization.',
    'ConsoleFilter': 'Editor-console query and clear filter. Runtime Console calls are inert when no editor host is bound; reads return no value rather than gameplay data.',
    'CameraRenderTarget': 'Optional camera capture dimensions used by RenderTexture. Capture requires an enabled camera; persistent texture assets are assigned through Camera.RenderTarget.',
}


def intro(name, language):
    text = OVERVIEWS.get(name, f'{name} declares the values and operations listed below. Types, defaults and overloads are part of its contract; it is not an editor panel.')
    result = f'<h1>{name}</h1><p>{html.escape(text)}</p>'
    if name.startswith('Audio'):
        result += '<p>See the <a href="../Audio/en.html">audio reference</a> for units, ranges, errors, effect parameters and playback/routing behavior.</p>' if name != 'Audio' else ''
    if name.startswith('Gui') or name in ('GUI','Frame','RectTransform','Rectangle'):
        result += '<p>See the <a href="../../gui/en.html">GUI guide</a> for layout, rendering, component write-back and input examples.</p>'
    return result


def updates():
    from scripts.build_public_docs import VERSION
    folders = [p for p in (ROOT/'docs/public').iterdir() if p.is_dir() and VERSION.fullmatch(p.name)]
    latest = max(folders, key=lambda p: tuple(map(int, p.name.split('-')[0].split('.'))))
    native, lua = cpp_types(), lua_types()
    for language, records in (('native', native), ('lua', lua)):
        for name, record in sorted(records.items()):
            path = latest/language/'reference'/name/'en.html'
            source = path.read_text(encoding='utf-8') if path.exists() else '<!doctype html><article class="doc">'+intro(name,language)+'</article>'
            generated = BEGIN+'\n'+supplement(language,name,record,native)+'\n'+END
            if BEGIN in source:
                updated = re.sub(re.escape(BEGIN)+r'[\s\S]*?'+re.escape(END), lambda _: generated, source)
            else:
                updated = source.replace('</article>', generated+'</article>')
            if updated != source or not path.exists(): yield path, updated


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--check', action='store_true'); args = parser.parse_args()
    pending = list(updates())
    if args.check:
        for path, _ in pending: print('API reference refresh needed:', path.relative_to(ROOT))
        return bool(pending)
    for path, source in pending:
        path.parent.mkdir(parents=True, exist_ok=True); path.write_text(source,encoding='utf-8',newline='\n')
    print(f'Refreshed {len(pending)} API reference pages. Run scripts/build_public_docs.py next.')
    return 0


if __name__ == '__main__':
    # Support both python -m scripts.sync_api_reference and direct invocation.
    import sys
    sys.path.insert(0,str(ROOT))
    raise SystemExit(main())
