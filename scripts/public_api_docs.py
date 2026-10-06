"""Versioned, reviewed API reference snapshots and HTML rendering."""
import argparse
import html
import json
from pathlib import Path
import re
try:
    from scripts.public_api import REPO, inventory, lua_inventory, tokens, split, method_open, end_pair, text
    from scripts.public_api_notes import TYPE_NOTES, PARAMETERS, describe
except ModuleNotFoundError:
    from public_api import REPO, inventory, lua_inventory, tokens, split, method_open, end_pair, text
    from public_api_notes import TYPE_NOTES, PARAMETERS, describe

ROOT = REPO / 'docs/public'

LUA_CALLS = {
 ('Entity','AddComponent'):('entity:AddComponent(typeName)', 'component copy'),
 ('Entity','AddOrReplaceComponent'):('entity:AddOrReplaceComponent(typeName, value)', 'component copy'),
 ('Entity','HasComponent'):('entity:HasComponent(typeName)', 'boolean'),
 ('Entity','GetComponent'):('entity:GetComponent(typeName)', 'component copy'),
 ('Entity','TryGetComponent'):('entity:TryGetComponent(typeName)', 'component copy or nil'),
 ('Entity','RemoveComponent'):('entity:RemoveComponent(typeName)', 'no value'),
 ('Entity','SetComponent'):('entity:SetComponent(typeName, value)', 'no value'),
 ('Entity','IsComponentEnabled'):('entity:IsComponentEnabled(typeName)', 'boolean'),
 ('Entity','SetComponentEnabled'):('entity:SetComponentEnabled(typeName, enabled)', 'no value'),
 ('EntityReference','TryGetWorldTransform'):('reference:TryGetWorldTransform()', 'success, Transform'),
 ('Scene','TryGetInheritedComponent'):('scene:TryGetInheritedComponent(entity, typeName)', 'component copy or nil'),
 ('Scene','GetEntities'):('scene:GetEntities([typeName])', 'one-based table of Entity'),
 ('Scene','AddSystem'):('scene:AddSystem(name, systemTable)', 'registered system table'),
 ('Scene','GetSystem'):('scene:GetSystem(name)', 'registered system table'),
 ('Scene','HasSystem'):('scene:HasSystem(name)', 'boolean'),
 ('Scene','RemoveSystem'):('scene:RemoveSystem(name)', 'boolean'),
 ('Scene','SetSystemEnabled'):('scene:SetSystemEnabled(name, enabled)', 'no value'),
 ('Scene','IsSystemEnabled'):('scene:IsSystemEnabled(name)', 'boolean'),
 ('Mat3','Get'):('matrix:Get(row, column)', 'number'), ('Mat3','Set'):('matrix:Set(row, column, value)', 'no value'),
 ('Mat4','Get'):('matrix:Get(row, column)', 'number'), ('Mat4','Set'):('matrix:Set(row, column, value)', 'no value'),
 ('Mat4','TryInverse'):('matrix:TryInverse()', 'success, Mat4'),
 ('Mat4','Decompose'):('matrix:Decompose()', 'success, Vec3 position, Quaternion rotation, Vec3 scale'),
 ('UUID','TryParse'):('Bazzalt.UUID.TryParse(text)', 'success, UUID'),
 ('UUID','Parse'):('Bazzalt.UUID.Parse(text)', 'UUID; raises an error for malformed text'),
 ('PostProcessingStack','SetEffect'):('stack:SetEffect(index, effect)', 'no value'),
 ('PostProcessingStack','AddEffect'):('stack:AddEffect(shaderAsset [, name])', 'effect copy; use SetEffect to commit edits'),
 ('PostProcessingStack','GetEffects'):('stack:GetEffects()', 'one-based table of effect copies'),
}
EXAMPLES = {
 'System':('class TurnSystem : public Bazzalt::ComponentSystem<Bazzalt::Transform> {\nprotected:\n    void OnUpdate(Bazzalt::Scene& scene, float dt) override {\n        auto view = GetView(scene.GetRegistry());\n        for (auto id : view) {\n            if (AreComponentsEnabled(view, id))\n                view.get<Bazzalt::Transform>(id).Rotate({0,1,0}, dt);\n        }\n    }\n};\n// Register from setup/OnCreate, not inside a system update:\n// scene.AddSystem<TurnSystem>();','-- Systems are named tables registered through Scene in Lua.\n-- See Scene.AddSystem for the actual Lua interface.'),
 'ComponentSystem':('class TurnSystem : public Bazzalt::ComponentSystem<Bazzalt::Transform> {\nprotected:\n    void OnUpdate(Bazzalt::Scene& scene, float dt) override {\n        auto view = GetView(scene.GetRegistry());\n        for (auto id : view) {\n            if (AreComponentsEnabled(view, id))\n                view.get<Bazzalt::Transform>(id).Rotate({0,1,0}, dt);\n        }\n    }\n};',''),
 'PrimitiveObject':('auto& primitive = GetEntity().GetComponent<Bazzalt::PrimitiveObject>();\nprimitive.Shape = Bazzalt::PrimitiveShape::Sphere;\nprimitive.Radius = 0.75f;','local primitive = self.Entity:GetComponent("PrimitiveObject")\nprimitive.Shape = Bazzalt.PrimitiveShape.Sphere\nprimitive.Radius = 0.75\nself.Entity:SetComponent("PrimitiveObject", primitive)'),
 'CameraPostProcessing':('auto& camera = GetEntity().GetComponent<Bazzalt::Camera>();\ncamera.PostProcessing.Enabled = true;\ncamera.PostProcessing.DepthOfField.Enabled = true;\ncamera.PostProcessing.DepthOfField.FocusDistance = 5.0f;','local camera = self.Entity:GetComponent("Camera")\ncamera.PostProcessing.Enabled = true\ncamera.PostProcessing.DepthOfField.Enabled = true\ncamera.PostProcessing.DepthOfField.FocusDistance = 5\nself.Entity:SetComponent("Camera", camera)'),
 'Mesh':('auto& mesh = GetEntity().GetComponent<Bazzalt::Mesh>();\nmesh.SetMaterial(0, material);','local mesh = self.Entity:GetComponent("Mesh")\nmesh:SetMaterial(0, material)\nself.Entity:SetComponent("Mesh", mesh)'),
 'Entity':('auto entity = GetEntity();\nif (auto* light = entity.TryGetComponent<Bazzalt::Light>())\n    light->Intensity = 1200.0f;', 'local light = self.Entity:TryGetComponent("Light")\nif light then\n    light.Intensity = 1200\n    self.Entity:SetComponent("Light", light)\nend'),
 'Scene':('auto* scene = Bazzalt::SceneManager::GetActiveScene();\nif (scene) {\n    auto cube = scene->CreateEntity("Cube");\n    cube.AddComponent<Bazzalt::PrimitiveObject>();\n}', 'local scene = Bazzalt.SceneManager.GetActiveScene()\nif scene then\n    local cube = scene:CreateEntity("Cube")\n    cube:AddComponent("PrimitiveObject")\nend'),
 'Transform':('auto world = GetEntity().GetWorldTransform();\nworld.Position.Y += 1.0f;\nGetEntity().SetWorldTransform(world);','local world = self.Entity:GetWorldTransform()\nworld.Position = world.Position + Bazzalt.Vec3.new(0, 1, 0)\nself.Entity:SetWorldTransform(world)'),
 'Camera':('auto& camera = GetEntity().GetComponent<Bazzalt::Camera>();\ncamera.VerticalFieldOfView = Bazzalt::ToRadians(60.0f);','local camera = self.Entity:GetComponent("Camera")\ncamera.VerticalFieldOfView = Bazzalt.ToRadians(60)\nself.Entity:SetComponent("Camera", camera)'),
 'CameraViewport':('auto rectangle = Bazzalt::CameraViewport::LeftHalf();','local rectangle = Bazzalt.CameraViewport.LeftHalf()'),
 'Light':('auto& light = GetEntity().GetComponent<Bazzalt::Light>();\nlight.Type = Bazzalt::LightType::Point;\nlight.Range = 6.0f;','local light = self.Entity:GetComponent("Light")\nlight.Type = Bazzalt.LightType.Point\nlight.Range = 6\nself.Entity:SetComponent("Light", light)'),
 'Material':('auto material = Bazzalt::Material::Create();\nmaterial.SetFloat("roughness", 0.4f);\nmaterial.ApplyTo(GetEntity());\n// Retain the handle; Destroy it during owned-resource cleanup.','local material = Bazzalt.Material.Create()\nmaterial:SetFloat("roughness", 0.4)\nmaterial:ApplyTo(self.Entity)\n-- Keep the handle; Destroy it when its owned runtime lifetime ends.'),
 'MaterialBuilder':('auto material = Bazzalt::MaterialBuilder()\n    .SetColor("baseColor", {0.2f, 0.4f, 0.8f, 1.0f})\n    .Build();','local material = Bazzalt.MaterialBuilder.new()\n    :SetColor("baseColor", Bazzalt.Vec4.new(0.2, 0.4, 0.8, 1))\n    :Build()'),
 'Shader':('auto shader = Bazzalt::Shader::Builtin(Bazzalt::ShaderPreset::Unlit);','local shader = Bazzalt.Shader.Builtin(Bazzalt.ShaderPreset.Unlit)'),
 'Time':('Bazzalt::Time::SetSlowMotion(0.25f);\n// Later:\nBazzalt::Time::RestoreTimeScale();','Bazzalt.Time.SetSlowMotion(0.25)\n-- Later:\nBazzalt.Time.RestoreTimeScale()'),
 'Input':('if (Bazzalt::Input::GetKeyDown(Bazzalt::KeyCode::Space)) {\n    // Perform a one-shot gameplay action.\n}','if Bazzalt.Input.GetKeyDown(Bazzalt.KeyCode.Space) then\n    -- Perform a one-shot gameplay action.\nend'),
 'UUID':('Bazzalt::UUID id;\nbool valid = Bazzalt::UUID::TryParse(savedText, id);\nif (valid) { /* Resolve through Scene or AssetManager. */ }','local valid, id = Bazzalt.UUID.TryParse(savedText)\nif valid then\n    -- Resolve through Scene or AssetManager.\nend'),
 'SceneManager':('auto* scene = Bazzalt::SceneManager::GetActiveScene();\n// Loading is deferred, not immediate:\nbool accepted = Bazzalt::SceneManager::LoadScene(sceneAssetUUID);','local scene = Bazzalt.SceneManager.GetActiveScene()\nlocal accepted = Bazzalt.SceneManager.LoadScene(sceneAssetUUID)'),
 'AssetManager':('auto asset = Bazzalt::AssetManager::GetAsset(assetUUID);\nif (asset && asset->State == Bazzalt::AssetState::Ready) {\n    // Runtime cache is ready.\n}','local asset = Bazzalt.AssetManager.GetAsset(assetUUID)\nif asset and asset.State == Bazzalt.AssetState.Ready then\n    -- Runtime cache is ready.\nend'),
 'Vec2':('Bazzalt::Vec2 point{320.0f, 240.0f};','local point = Bazzalt.Vec2.new(320, 240)'),
 'Vec3':('auto direction = Bazzalt::Vec3{1, 0, 1}.Normalized();','local direction = Bazzalt.Vec3.new(1, 0, 1):Normalized()'),
 'Vec4':('Bazzalt::Vec4 color{0.2f, 0.4f, 0.8f, 1.0f};','local color = Bazzalt.Vec4.new(0.2, 0.4, 0.8, 1)'),
 'Mat3':('Bazzalt::Mat3 matrix;\nmatrix(0, 0) = 2.0f;','local matrix = Bazzalt.Mat3.new()\nmatrix:Set(0, 0, 2)'),
 'Mat4':('auto matrix = Bazzalt::Mat4::Translation({0, 1, 0});\nauto moved = matrix.TransformPoint({0, 0, 0});','local matrix = Bazzalt.Mat4.Translation(Bazzalt.Vec3.new(0, 1, 0))\nlocal moved = matrix:TransformPoint(Bazzalt.Vec3.new())'),
 'Quaternion':('auto rotation = Bazzalt::Quaternion::FromAxisAngle({0,1,0}, Bazzalt::ToRadians(90));','local rotation = Bazzalt.Quaternion.FromAxisAngle(Bazzalt.Vec3.new(0,1,0), Bazzalt.ToRadians(90))'),
 'SceneRay':('Bazzalt::SceneRay ray{{0,0,5}, {0,0,-1}};','local ray = Bazzalt.SceneRay.new()\nray.Origin = Bazzalt.Vec3.new(0,0,5)\nray.Direction = Bazzalt.Vec3.new(0,0,-1)'),
 'SceneQueryOptions':('Bazzalt::SceneQueryOptions options;\noptions.MaxDistance = 100.0f;','local options = Bazzalt.SceneQueryOptions.new()\noptions.MaxDistance = 100'),
 'SceneQueryHit':('auto hit = scene->Raycast(ray);\nif (hit) { auto selected = hit.Target; }','local hit = scene:Raycast(ray)\nif hit.Target:IsValid() then\n    local selected = hit.Target\nend'),
 'SceneEnvironment':('auto environment = scene->GetEnvironment();\nenvironment.ImageBasedLighting = false;\nscene->SetEnvironment(environment);','local environment = scene:GetEnvironment()\nenvironment.ImageBasedLighting = false\nscene:SetEnvironment(environment)'),
 'Behavior':('COMPONENT(Example) {\npublic:\n    PROPERTY(float, Speed, 4.0f)\n    void OnUpdate(float deltaTime) override { (void)deltaTime; }\n};','local Example = {}\nfunction Example:OnUpdate(deltaTime)\n    local entity = self.Entity\nend\nreturn Example'),
 'MathFunctions':('float angle = Bazzalt::ToRadians(90.0f);','local angle = Bazzalt.ToRadians(90)'),
 'PostProcessingStack':('auto& effect = camera.PostProcessing.CustomEffects.AddEffect(shaderAssetUUID, "Custom");\neffect.SetParameter(Bazzalt::PostProcessParameter::Float("strength", 0.5f));','local stack = camera.PostProcessing.CustomEffects\nlocal effect = stack:AddEffect(shaderAssetUUID, "Custom")\neffect.Parameters = { Bazzalt.PostProcessParameter.Float("strength", 0.5) }\nstack:SetEffect(0, effect)\nself.Entity:SetComponent("Camera", camera)'),
}

def parameter_info(signature):
    seq = tokens(signature)
    opening = method_open(seq)
    if opening is None: return []
    closing = end_pair(seq, opening, '(', ')')
    result = []
    for part in split(seq[opening+1:closing], ','):
        if not part: continue
        declaration = text(part)
        before = part[:part.index('=')] if '=' in part else part
        name = next((x for x in reversed(before) if re.fullmatch(r'[a-z][A-Za-z0-9_]*', x)), None)
        if name:
            result.append(dict(name=name, declaration=declaration, explanation=PARAMETERS.get(name, 'Operand/value of the type shown in the declaration; see this method\'s behavior and coordinate-space description.')))
    return result


def snapshot():
    native = inventory()
    for type_ in native:
        type_['description'] = TYPE_NOTES[type_['name']]
        for member in type_['members']:
            member['description'] = describe(type_['name'], member)
            member['parameters'] = parameter_info(member['signature']) if member['kind'] == 'method' else []
    by_name = {t['name']:t for t in native}
    lua = []
    for registration in lua_inventory():
        name, cpp = registration['name'],registration['cpp']
        source = by_name[cpp]
        entry = dict(name=name, cpp=cpp, header=source['header'], kind=source['kind'], description=source['description'], constructor=registration['constructor'], members=[])
        for method in registration['members']:
            matches = [m for m in source['members'] if m['name'] == method]
            if not matches:
                sample = dict(name=method, kind='method', signature='', parameters=[], description=describe(cpp,dict(name=method,kind='method',signature='')))
                matches = [sample]
            for original in matches:
                member = dict(original)
                member['name'] = method
                if (name,method) in LUA_CALLS:
                    member['call'],member['result'] = LUA_CALLS[name,method]
                else:
                    static = source['kind'] in ('enum','namespace') or 'static' in original['signature'] or name in ('Time','Input','SceneManager','AssetManager')
                    prefix = 'Bazzalt' if name == 'MathFunctions' else 'Bazzalt.'+name if static else 'value'
                    separator = '.' if static else ':'
                    if member['kind'] in ('field','value','alias'):
                        member['call'] = prefix+'.'+method
                        member['result'] = 'value of the declared type'
                    else:
                        names = [p['name']+(' = '+p['declaration'].split('=',1)[1].strip() if '=' in p['declaration'] else '') for p in member['parameters']]
                        member['call'] = prefix+separator+method+'('+', '.join(names)+')'
                        member['result'] = lua_result(original['signature'])
                entry['members'].append(member)
        if name in ('Vec2','Vec3','Vec4'):
            for op,call,note in [('+','left + right','Returns component-wise addition of two vectors.'),('-','left - right','Returns component-wise subtraction of two vectors. A unary-minus binding is not registered.'),('*','vector * scalar  -- or scalar * vector','Returns a vector scaled by a number. Component-wise vector×vector multiplication is not bound in Lua.'),('/','vector / scalar','Returns a vector divided by a nonzero scalar. Do not divide by zero.')]:
                entry['members'].append(dict(name='operator'+op,kind='method',signature='',call=call,result=name,parameters=[],description=note))
        elif name in ('Mat3','Mat4','Quaternion'):
            call = 'left * right'
            note = 'Returns composed matrices; matrix×Vec3 is also supported.' if name=='Mat3' else 'Returns composed matrices; matrix×Vec4 is also supported.' if name=='Mat4' else 'Returns quaternion rotation composition (left times right).'
            entry['members'].append(dict(name='operator*',kind='method',signature='',call=call,result='matrix or transformed vector' if name.startswith('Mat') else 'Quaternion',parameters=[],description=note))
        if name in ('Entity','UUID'):
            entry['members'] += [dict(name='operator==',kind='method',signature='',call='left == right',result='boolean',parameters=[],description='Compares entity registry/ID or UUID halves; does not compare display names.')]
        lua.append(entry)
    lua.append(dict(name='Behavior',cpp='Behavior',header='Script.h',kind='table',constructor=False,description='A returned Lua table with optional lifecycle callbacks. Each attachment receives self.Entity and its authored property overrides in an isolated VM.',members=[dict(name=m['name'],kind='method',signature='',call='function Behavior:'+m['name']+'('+('deltaTime' if m['name']=='OnUpdate' else 'fixedDeltaTime' if m['name']=='OnFixedUpdate' else '')+')',result='no required return value',parameters=m['parameters'],description=m['description']) for m in by_name['Behavior']['members'] if m['name'] in ('OnCreate','OnUpdate','OnFixedUpdate','OnDestroy')] + [dict(name='Entity',kind='field',signature='',call='self.Entity',result='bound Entity',parameters=[],description='Host-supplied owning entity handle. Do not replace it or reuse it after the host destroys the scene.')]))
    return dict(cpp=native,lua=lua)


def lua_result(signature):
    seq=tokens(signature);opening=method_open(seq)
    if opening is None: return 'value'
    result=text(seq[:opening-1])
    result=re.sub(r'\b(static|virtual|inline|constexpr|const|friend)\b', '', result).strip().replace('&','').strip()
    if result == 'void': return 'no value'
    if 'optional' in result: return result.replace('std::optional<','').rstrip('>')+' or nil'
    if 'vector' in result: return 'one-based table of result values'
    if result in ('float','double','int','std::size_t','std::uint32_t','std::uint64_t','Entity::Id'): return 'number'
    if result == 'bool': return 'boolean'
    if '*' in result: return result.replace('*','').strip()+' or nil (non-owning)'
    return result or 'value'


def slug(name):
    if name.startswith('~'): return 'destructor-'+name[1:]
    if name.startswith('operator'):
        suffix=name[len('operator'):]
        return 'operator-'+{'+':'add','-':'subtract','*':'multiply','/':'divide','+=':'add-assign','-=':'subtract-assign','*=':'multiply-assign','/=':'divide-assign','()':'index','==':'equal','!=':'not-equal','<':'less','=':'assign','bool':'bool'}.get(suffix,suffix)
    return re.sub(r'[^a-zA-Z0-9_-]+','-',name).strip('-')


def category(type_):
    header=type_['header']
    if type_['kind']=='enum': return 'Enumerations'
    if header=='Math.h' or header=='UUID.h': return 'Math and identifiers'
    if header.startswith('Components/') or type_['name']=='Component': return 'Components'
    if header in ('Scene.h','Entity.h','EntityReference.h','SceneManager.h','SceneQuery.h','SceneEnvironment.h'): return 'Scenes and entities'
    if header in ('Material.h','MaterialBuilder.h','Shader.h','PostProcessing.h'): return 'Materials and effects'
    if header in ('Input.h','Time.h','System.h','Script.h') and type_['name'] not in ('ScriptModuleApi','ScriptRuntimeAccess'): return 'Gameplay and systems'
    if header in ('Asset.h','AssetManager.h','ModelAsset.h'): return 'Assets and models'
    return 'Serialization and module contracts'


def reference_nodes(data, language):
    groups={}
    for type_ in data[language]:
        groups.setdefault(category(type_),[]).append(dict(id=('native' if language=='cpp' else 'lua')+'/reference/'+slug(type_['name']),title=type_['name']))
    return [dict(title=name,children=sorted(pages,key=lambda p:p['title'])) for name,pages in groups.items()]


def render(type_, language):
    escape=html.escape
    name=type_['name']
    parts=[f'<h1>{escape(name)}</h1>',f'<p class="lead">{escape(type_["description"])}</p>',f'<p><strong>{"C++ header" if language=="cpp" else "Underlying public header"}:</strong> <code>Bazzalt/{escape(type_["header"])}</code>.</p>']
    parts.append('<p>Read signatures together with the behavior notes below. The <a href="../en.html">reference introduction</a> explains arguments, return values, value copies, and errors.</p>')
    if language=='lua':
        parts.append('<p>In call notation, <code>argument = default</code> describes an optional argument and its default; Lua does not use named arguments here. Supply the value positionally or omit it. The C++ declaration below is comparison data, not Lua syntax.</p>')
        if type_['constructor']:
            alternatives = {'Vec2':['','value','x, y'],'Vec3':['','value','x, y, z'],'Vec4':['','value','x, y, z, w'],'Quaternion':['','x, y, z, w'],'Mat3':['','diagonal'],'Mat4':['','diagonal'],'UUID':['','high, low'],'EntityReference':['','id'],'MaterialBuilder':['','shader']}.get(name,[''])
            calls = '\n'.join('Bazzalt.'+name+'.new('+args+')' for args in alternatives)
            parts.append('<h2 id="construction">Construction</h2><pre><code>'+escape(calls)+'</code></pre><p>Each line is an accepted construction form. The argument names stand for values you supply; an empty call uses default values. Plain data types construct with their declared default fields.</p>')
        else:
            parts.append('<p>This is an enum/static service, host-owned scene, or returned behavior table, not a constructible Lua value. Use the static service or the host-provided value.</p>')
        if type_['header'].startswith('Components/') or name=='Component':
            parts.append('<div class="notice"><p>Component values returned by Entity are copies. After changing fields or methods, call entity:SetComponent(typeName, value). Derived components inherit Component.Enabled, IsEnabled and SetEnabled; core Identity, Hierarchy, Name and Transform have entity-level restrictions.</p></div>')
    else:
        parts.append('<pre><code>#include &lt;Bazzalt/'+escape(type_['header'])+'&gt;</code></pre>')
        parts.append('<pre><code>'+escape(type_.get('declaration',''))+'</code></pre>')
        if 'Component' in type_.get('declaration','') and name != 'Component':
            parts.append('<p>Component-derived types also inherit Enabled, IsEnabled and SetEnabled. Entity APIs protect required default components. References into scene storage must not outlive their entity/scene.</p>')
    example=EXAMPLES.get(type_.get('cpp',name))
    if example and name in ('System','ComponentSystem'):
        example=(example[0].replace('for (auto id : view) {', 'for (auto id : view) {\n            if (scene.GetEntity(static_cast<Bazzalt::Entity::Id>(id)).GetUUID().IsRoot()) continue;'),example[1])
    if example:
        parts.append('<h2 id="example">Example</h2><p>Gameplay snippets run inside a behavior callback or helper. Variables such as scene, camera, savedText and assetUUID are existing values, not automatically declared global variables.</p><pre><code>'+escape(example[0 if language=='cpp' else 1])+'</code></pre>')
    grouped={}
    for member in type_['members']: grouped.setdefault(member['name'],[]).append(member)
    for member_name, overloads in grouped.items():
        anchor=slug(member_name)
        parts.append(f'<h2 id="{anchor}">{escape(member_name)}</h2>')
        declarations=list(dict.fromkeys(m['signature'] if language=='cpp' else m['call'] for m in overloads))
        parts.append('<pre><code>'+escape('\n'.join(declarations))+'</code></pre>')
        for description in dict.fromkeys(m['description'] for m in overloads): parts.append('<p>'+escape(description)+'</p>')
        if any(m['kind']=='method' for m in overloads):
            if language=='lua':
                parts.append('<p><strong>Returns:</strong> '+escape('; '.join(dict.fromkeys(m['result'] for m in overloads)))+'.</p>')
            else:
                parts.append('<p><strong>Result:</strong> the return type at the start of each declaration. <code>void</code> returns no value; <code>bool</code> reports true/false; <code>&amp;</code> is a reference rather than a copy. Constructors create a value; deleted declarations cannot be called.</p>')
            parameters={p['name']:p for m in overloads for p in m['parameters']}
            if parameters:
                parts.append('<table><thead><tr><th>Argument</th><th>Meaning</th></tr></thead><tbody>')
                for p in parameters.values(): parts.append('<tr><td><code>'+escape(p['declaration'])+'</code></td><td>'+escape(p['explanation'])+'</td></tr>')
                parts.append('</tbody></table>')
            if language=='lua': parts.append('<p>Invalid arguments and C++ exceptions are reported as Lua errors at the protected script boundary. Check the specific absence/false result rules above; not every missing value is an error.</p>')
        if language=='lua' and any(m.get('signature') for m in overloads):
            parts.append('<details><summary>Underlying C++ declaration / default values</summary><pre><code>'+escape('\n'.join(dict.fromkeys(m['signature'] for m in overloads if m['signature'])))+'</code></pre></details>')
    return '\n'.join(parts)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');parser.add_argument('--version',default='0.5.0');args=parser.parse_args()
    current=snapshot();path=ROOT/'_api'/f'{args.version}.json'
    encoded=json.dumps(current,ensure_ascii=False,indent=2)+'\n'
    if args.check:
        if not path.exists() or path.read_text(encoding='utf-8')!=encoded:
            print('API snapshot is stale. Review notes/bindings, then run scripts/public_api_docs.py.');return 1
    else:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(encoded,encoding='utf-8',newline='\n')
    print(f'API snapshot: {len(current["cpp"])} C++ types/scopes, {len(current["lua"])} Lua types/tables.');return 0

if __name__=='__main__': raise SystemExit(main())
