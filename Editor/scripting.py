"""Editor-owned C++ behavior discovery and compilation."""
from __future__ import annotations
from dataclasses import dataclass, field
import ast, hashlib, json, os, re, subprocess
from pathlib import Path

_COMPONENT=re.compile(r"\bCOMPONENT\s*\(\s*([A-Za-z_]\w*)\s*\)")
_PROPERTY=re.compile(r"\bPROPERTY\s*\(\s*([^,]+?)\s*,\s*([A-Za-z_]\w*)\s*,\s*([^,\)]+)(?:,([^\)]*))?\)")

class ScriptValidationError(ValueError):pass

def _CodeMask(text, strings=True):
    pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern,lambda m:re.sub(r"[^\n]"," ",m.group()) if strings or m.group().startswith(("//","/*")) else m.group(),text)

@dataclass(frozen=True)
class ScriptProperty:
    type: str; name: str; default: str; attributes: tuple[str,...]=()
    rules: dict=field(default_factory=dict)
@dataclass(frozen=True)
class ScriptDescriptor:
    path: Path; name: str; properties: tuple[ScriptProperty,...]=field(default_factory=tuple)
@dataclass(frozen=True)
class Diagnostic:
    level: str; message: str; source: str="Compiler";localization_key: str=""
@dataclass(frozen=True)
class BuildResult:
    success: bool; outputs: tuple[Path,...]=(); diagnostics: tuple[Diagnostic,...]=()

class ScriptCompiler:
    """Incremental compiler using shared Hub tools or explicit local overrides."""
    def __init__(self, project: str|Path, engine_root: str|Path|None=None, app_root: str|Path|None=None, tools: dict|None=None):
        self.project=Path(project).resolve();self.engine_root=Path(engine_root or Path(__file__).resolve().parents[1])
        self.app_root=Path(app_root or Path(os.path.abspath(os.path.dirname(os.sys.executable))))
        self.cache=self.project/".bazzalt"/"ScriptAssemblies";self.state_file=self.cache/"build-state.json"
        self.Diagnostics=[]
        self.Tools = dict(tools or {})
    @staticmethod
    def Inspect(path: str|Path)->ScriptDescriptor|None:
        path=Path(path)
        try:text=path.read_text(encoding="utf-8")
        except (OSError,UnicodeError):return None
        if path.suffix.lower()==".lua":
            from Editor.lua_declarations import ReadDeclarations,DeclarationError
            try:declaration=ReadDeclarations(text)
            except DeclarationError as error:raise ScriptValidationError(f"{path}: {error}") from error
            if declaration:
                name,fields=declaration
                properties=tuple(ScriptProperty(*field) for field in fields)
                if len({p.name for p in properties})!=len(properties):raise ScriptValidationError(f"{path}: duplicate Lua properties")
                return ScriptDescriptor(path.resolve(),name,properties)
            # Metadata is declarative comments; never execute scripts to inspect them.
            declarations=re.findall(r"^\s*--\s*COMPONENT\s*\(\s*([A-Za-z_]\w*)\s*\)\s*$",text,re.M)
            if len(declarations)>1:raise ScriptValidationError(f"{path}: declare at most one Lua COMPONENT")
            name=declarations[0] if declarations else path.stem
            if not re.fullmatch(r"[A-Za-z_]\w*",name):raise ScriptValidationError(f"{path}: invalid Lua component name")
            properties=tuple(ScriptProperty(m.group(1).strip(),m.group(2),m.group(3).strip()) for line in text.splitlines() if line.lstrip().startswith("--") for m in _PROPERTY.finditer(line))
            if len({p.name for p in properties})!=len(properties):raise ScriptValidationError(f"{path}: duplicate Lua properties")
            return ScriptDescriptor(path.resolve(),name,properties)
        mask=_CodeMask(text);matches=list(_COMPONENT.finditer(mask))
        if not matches:return None
        if len(matches)!=1:raise ScriptValidationError(f"{path}: declare exactly one COMPONENT per source file")
        match=matches[0];start=mask.find("{",match.end());end=start;depth=0
        if start<0:raise ScriptValidationError(f"{path}: component {match.group(1)} has no body")
        for end in range(start,len(mask)):
            if mask[end]=="{":depth+=1
            elif mask[end]=="}":
                depth-=1
                if depth==0:break
        if depth:raise ScriptValidationError(f"{path}: component {match.group(1)} has an incomplete body")
        body=_CodeMask(text,False)[start:end];body_mask=mask[start:end]
        properties=tuple(ScriptProperty(m.group(1).strip(),m.group(2),m.group(3).strip(),tuple(a.strip() for a in (m.group(4) or "").split(",") if a.strip())) for m in _PROPERTY.finditer(body) if body_mask[m.start():].startswith("PROPERTY"))
        names=[p.name for p in properties]
        if len(names)!=len(set(names)):raise ScriptValidationError(f"{path}: component {match.group(1)} has duplicate PROPERTY names")
        from Editor.property_rules import CheckTypeRules,RuleError
        decorated=[]
        for prop in properties:
            rules={}
            for attribute in prop.attributes:
                if '=' not in attribute:continue
                key,raw=attribute.split('=',1)
                try:rules[key.strip()]={'true':True,'false':False}[raw.strip()] if raw.strip() in ('true','false') else ast.literal_eval(raw.strip())
                except (ValueError,SyntaxError):raise ScriptValidationError(f"{path}: property options must be literals")
            try:CheckTypeRules(prop.type,rules)
            except RuleError as error:raise ScriptValidationError(f"{path}: {error}") from error
            decorated.append(ScriptProperty(prop.type,prop.name,prop.default,prop.attributes,rules))
        return ScriptDescriptor(path.resolve(),match.group(1),tuple(decorated))
    def Discover(self)->list[ScriptDescriptor]:
        assets=self.project/"Assets"
        paths=sorted((path for path in assets.rglob("*") if path.is_file() and path.suffix.lower() in {".cpp",".lua"}),key=str) if assets.is_dir() else []
        return self._Validate(paths)
    def _Validate(self,paths):
        self.Diagnostics=[];groups={}
        for path in dict.fromkeys(Path(path).resolve() for path in paths):
            try:descriptor=self.Inspect(path)
            except ScriptValidationError as error:
                self.Diagnostics.append(Diagnostic("error",str(error)));continue
            if descriptor:groups.setdefault(descriptor.name,[]).append(descriptor)
        result=[]
        for name,descriptors in groups.items():
            if len(descriptors)>1:
                files=", ".join(str(value.path) for value in descriptors)
                self.Diagnostics.append(Diagnostic("error",f"Ambiguous component '{name}' declared in: {files}. Rename one COMPONENT type."))
            else:result.append(descriptors[0])
        return result
    def ValidateDescriptor(self,descriptor):
        discovered=self.Discover()
        for value in discovered:
            if value.path==descriptor.path.resolve() and value.name==descriptor.name:return value
        raise ScriptValidationError("\n".join(value.message for value in self.Diagnostics) or f"Invalid or missing component source: {descriptor.path}")
    def _compiler(self)->Path|None:
        from bazzalt.tools import FindCompiler
        return FindCompiler(self.Tools.get("compiler_path", ""), (self.app_root,self.engine_root))
    def _sdk(self)->Path|None:
        override = self.Tools.get("sdk_path", "")
        roots = (Path(override).expanduser(),) if override else (self.app_root/"ScriptSDK",self.engine_root/"build"/"ScriptSDK")
        for root in roots:
            if (root/"include/Bazzalt/Scene.h").is_file() and (root/"include/entt/entity/registry.hpp").is_file():return root
        return None
    def _link_library(self,sdk:Path)->Path|None:
        names=("Bazzalt.lib",) if os.name=="nt" else ("libBazzalt.dylib",) if os.sys.platform=="darwin" else ("libBazzalt.so",)
        for folder in (sdk/"lib/Release",sdk/"lib"):
            for name in names:
                candidate=folder/name
                if candidate.is_file():return candidate
        return None
    def Build(self, used: list[str|Path])->BuildResult:
        try:
            return self._Build(used)
        except (OSError, ValueError) as error:
            return BuildResult(False, diagnostics=(Diagnostic("error", str(error)),))

    def _Build(self, used: list[str|Path])->BuildResult:
        used=list(dict.fromkeys(Path(path).resolve() for path in used if Path(path).suffix.lower()!=".lua"));assets=self.project/"Assets"
        project_sources=[path for path in assets.rglob("*") if path.is_file() and path.suffix.lower() in {".cpp",".lua"}] if assets.is_dir() else []
        validated=self._Validate([*project_sources,*used]);by_path={value.path:value for value in validated}
        if self.Diagnostics:return BuildResult(False,diagnostics=tuple(self.Diagnostics))
        missing=[str(path) for path in used if path not in by_path]
        if missing:return BuildResult(False,diagnostics=(Diagnostic("error","Invalid or missing component sources: "+", ".join(missing)),))
        descriptors=[by_path[path] for path in used]
        if not descriptors:return BuildResult(True)
        compiler=self._compiler()
        if compiler is None:return BuildResult(False,diagnostics=(Diagnostic("error","Build tools are missing. Install them in Hub or set a compiler path in Preferences.",localization_key="scripting.toolchain_missing"),))
        sdk=self._sdk();library=self._link_library(sdk) if sdk else None
        if not library:return BuildResult(False,diagnostics=(Diagnostic("error","The matching Bazzalt script SDK/link library is missing. Rebuild or repair this editor installation.",localization_key="scripting.sdk_missing"),))
        self.cache.mkdir(parents=True,exist_ok=True)
        try:state=json.loads(self.state_file.read_text(encoding="utf-8"))
        except (OSError,json.JSONDecodeError):state={}
        outputs=[];diagnostics=[]
        headers=b"".join(str(path.relative_to(sdk)).encode()+path.read_bytes() for path in sorted((sdk/"include").rglob("*.h")))
        headers+=b"".join(str(path.relative_to(sdk)).encode()+path.read_bytes() for path in sorted((sdk/"include").rglob("*.hpp")))
        import struct
        compiler_stat=compiler.stat()
        compiler_identity=f"{compiler.resolve()}:{compiler_stat.st_size}:{compiler_stat.st_mtime_ns}:{struct.calcsize('P')}".encode("utf-8")
        sdk_digest=hashlib.sha256(headers+library.read_bytes()+compiler_identity).digest()
        for descriptor in descriptors:
            digest=hashlib.sha256(descriptor.path.read_bytes()+b"\0bazzalt-script-linked-sdk-1"+sdk_digest).hexdigest()
            suffix=".dll" if os.name=="nt" else ".dylib" if os.sys.platform=="darwin" else ".so"
            output=self.cache/f"{descriptor.name}-{digest[:12]}{suffix}";outputs.append(output)
            if state.get(str(descriptor.path))==digest and output.exists():continue
            wrapper=self.cache/f"{descriptor.name}-{digest[:12]}.module.cpp"
            wrapper.write_text(self._Wrapper(descriptor),encoding="utf-8")
            command=[str(compiler),"-std=c++20","-shared","-fvisibility=hidden",f"-I{sdk/'include'}",str(wrapper),str(library),"-o",str(output)]
            if os.name=="nt":
                import struct
                command.append("--target=" + ("x86_64" if struct.calcsize("P")==8 else "i686") + "-pc-windows-msvc")
            if os.name=="nt":command.extend(["-fms-runtime-lib=dll","-D_ITERATOR_DEBUG_LEVEL=0"])
            else:
                command.extend(["-fPIC",f"-Wl,-rpath,{sdk/'lib'}"])
                if os.sys.platform!="darwin":command.append("-Wl,--no-undefined")
            process=subprocess.run(command,cwd=self.project,text=True,capture_output=True,encoding="utf-8",errors="replace",creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
            for line in (process.stdout+"\n"+process.stderr).splitlines():
                if line.strip():diagnostics.append(Diagnostic("error" if "error:" in line.lower() else "warning" if "warning:" in line.lower() else "info",line.strip()))
            if process.returncode:return BuildResult(False,tuple(outputs),tuple(diagnostics))
            state[str(descriptor.path)]=digest
        self.state_file.write_text(json.dumps(state,indent=2),encoding="utf-8")
        return BuildResult(True,tuple(outputs),tuple(diagnostics))

    @staticmethod
    def _Wrapper(descriptor: ScriptDescriptor)->str:
        setters=[]
        for prop in descriptor.properties:
            kind=prop.type.replace("const","").replace("&","").strip()
            if kind in ("bool",):body=f'self->{prop.name}=std::strcmp(value,"true")==0||std::strcmp(value,"1")==0;return true;'
            elif kind in ("string","std::string"):body=f'self->{prop.name}=value;return true;'
            elif kind in ("EntityReference","Bazzalt::EntityReference"):
                body=f'{{Bazzalt::UUID id;if(std::strcmp(value,"0")!=0&&std::strcmp(value,"")!=0&&!Bazzalt::UUID::TryParse(value,id))return false;self->{prop.name}=Bazzalt::EntityReference(id);return true;}}'
            elif kind in ("Entity","Bazzalt::Entity"):
                body=f'{{Bazzalt::UUID id;if(std::strcmp(value,"0")!=0&&std::strcmp(value,"")!=0&&!Bazzalt::UUID::TryParse(value,id))return false;auto* scene=Bazzalt::SceneManager::GetActiveScene();self->{prop.name}=id&&scene?scene->GetEntity(id):Bazzalt::Entity{{}};return true;}}'
            elif kind in ("Material","Bazzalt::Material","Shader","Bazzalt::Shader","Texture","Bazzalt::Texture"):
                type_name=kind.split("::")[-1];body=f'{{Bazzalt::UUID id;if(std::strcmp(value,"0")!=0&&!Bazzalt::UUID::TryParse(value,id))return false;self->{prop.name}=Bazzalt::{type_name}::Load(id);return !id||self->{prop.name}.IsValid();}}'
            elif kind in ("Vec2","Bazzalt::Vec2"):body=f'return std::sscanf(value,"%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y)==2;'
            elif kind in ("Vec3","Bazzalt::Vec3"):body=f'return std::sscanf(value,"%f,%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y,&self->{prop.name}.Z)==3;'
            elif kind in ("Vec4","Bazzalt::Vec4"):body=f'return std::sscanf(value,"%f,%f,%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y,&self->{prop.name}.Z,&self->{prop.name}.W)==4;'
            else:body=f'{{std::istringstream input(value);input>>self->{prop.name};return !input.fail();}}'
            setters.append(f'if(std::strcmp(name,"{prop.name}")==0){{{body}}}')
        source=str(descriptor.path).replace("\\","/").replace('"','\\"')
        name=descriptor.name;setter="".join(setters)
        wrapper=f'''#include <Bazzalt/Script.h>\n#include <cstdio>\n#include <cstring>\n#include <sstream>\n#include <string>\n#include "{source}"\n#if defined(_WIN32)\n#define BAZZALT_SCRIPT_EXPORT __declspec(dllexport)\n#else\n#define BAZZALT_SCRIPT_EXPORT __attribute__((visibility("default")))\n#endif\nnamespace {{\nvoid* Create(){{return new {name}();}}\nvoid Destroy(void* p){{delete static_cast<{name}*>(p);}}\nvoid OnCreate(void* p){{static_cast<{name}*>(p)->OnCreate();}}\nvoid OnUpdate(void* p,float dt){{static_cast<{name}*>(p)->OnUpdate(dt);}}\nvoid OnDestroy(void* p){{static_cast<{name}*>(p)->OnDestroy();}}\nbool SetProperty(void* p,const char* name,const char* value){{auto* self=static_cast<{name}*>(p);{setter}return false;}}\nconst Bazzalt::ScriptModuleApi Api{{Bazzalt::ScriptAbiVersion,"{name}",&Create,&Destroy,&OnCreate,&OnUpdate,&OnDestroy,&SetProperty}};\n}}\nextern "C" BAZZALT_SCRIPT_EXPORT const Bazzalt::ScriptModuleApi* BazzaltGetScriptModuleV1(){{return &Api;}}\n'''
        wrapper += '\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltBindTimeV1(Bazzalt::Detail::TimeState* state){Bazzalt::ScriptRuntimeAccess::BindTime(state);}\n'
        wrapper += '\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltBindInputV1(Bazzalt::Detail::InputState* state){Bazzalt::ScriptRuntimeAccess::BindInput(state);}\n'
        wrapper = '#include <Bazzalt/Material.h>\n'+wrapper
        wrapper = '#include <Bazzalt/EntityReference.h>\n'+wrapper
        wrapper += '\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltBindEntitiesV1(Bazzalt::Detail::EntityServices* services){Bazzalt::Detail::BoundEntityServices=services;}\n'
        wrapper += '\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltBindMaterialsV1(Bazzalt::Detail::MaterialServices* services){Bazzalt::Detail::BoundMaterialServices=services;}\n'
        wrapper += f'\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltFixedUpdateV1(void* p,float dt){{static_cast<{name}*>(p)->OnFixedUpdate(dt);}}\n'
        return wrapper.replace(f"void* Create(){{return new {name}();}}",f"void* Create(const char* entity){{auto* value=new {name}();Bazzalt::ScriptRuntimeAccess::Bind(*value,entity);return value;}}")

class ScriptAttachments:
    """Project-side attachment manifest, keyed by stable scene/entity UUIDs."""
    def __init__(self,project: str|Path):
        self.path=Path(project).resolve()/".bazzalt"/"ScriptAttachments.json";self.values={}
        try:self.values=json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError,json.JSONDecodeError):pass
    def Save(self):
        self.path.parent.mkdir(parents=True,exist_ok=True);self.path.write_text(json.dumps({"version":1,"entities":self.values.get("entities",{})},indent=2),encoding="utf-8")
    def For(self,entity: str)->list[dict]:
        # Legacy manifests could contain two sources with one type name. Keep
        # only the first visible; never merge their properties or run both.
        result=[];names=set();sources=set()
        for value in self.values.setdefault("entities",{}).setdefault(entity,[]):
            name=value.get("type");source=str(Path(value.get("source","")).resolve())
            if name in names or source in sources:continue
            names.add(name);sources.add(source);result.append(value)
        return result
    def Attach(self,entity: str,descriptor: ScriptDescriptor)->bool:
        entries=self.values.setdefault("entities",{}).setdefault(entity,[])
        if any(v.get("type")==descriptor.name or Path(v["source"]).resolve()==descriptor.path.resolve() for v in entries):return False
        names=[p.name for p in descriptor.properties]
        if len(names)!=len(set(names)):raise ScriptValidationError(f"Duplicate properties in component {descriptor.name}")
        entries.append({"source":str(descriptor.path),"type":descriptor.name,"enabled":True,"properties":{p.name:_PropertyDefault(p) for p in descriptor.properties}});self.Save();return True
    def Remove(self,entity: str,type_name: str)->bool:
        entries=self.values.setdefault("entities",{}).setdefault(entity,[]);remaining=[v for v in entries if v.get("type")!=type_name]
        if len(remaining)==len(entries):return False
        self.values["entities"][entity]=remaining;self.Save();return True
    def UsedSources(self)->list[str]:return list(dict.fromkeys(v.get("source","") for entries in self.values.get("entities",{}).values() for v in entries if v.get("enabled",True) and v.get("source")))
    def RuntimeBindings(self,outputs: tuple[Path,...])->list[dict]:
        for entity,entries in self.values.get("entities",{}).items():
            names=[value.get("type") for value in entries]
            paths=[str(Path(value.get("source","")).resolve()) for value in entries]
            if len(names)!=len(set(names)) or len(paths)!=len(set(paths)):raise ScriptValidationError(f"Entity {entity} has duplicate script component types or sources. Remove the duplicate component and add it again.")
        sources=[source for source in self.UsedSources() if Path(source).suffix.lower()!=".lua"]
        modules={str(Path(source).resolve()):str(output) for source,output in zip(sources,outputs)};result=[]
        for source in self.UsedSources():
            if Path(source).suffix.lower()==".lua":modules[str(Path(source).resolve())]=str(Path(source).resolve())
        for entity,entries in self.values.get("entities",{}).items():
            for value in entries:
                source=str(Path(value.get("source","")).resolve())
                if value.get("enabled",True) and source in modules:result.append({"module":modules[source],"entity":entity,"type":value.get("type",Path(source).stem),"properties":dict(value.get("properties",{}))})
        return result
    def LuaSceneBindings(self)->list[dict]:
        return [{"entity":entity,"source":value["source"],"type":value["type"],"enabled":value.get("enabled",True),"properties":dict(value.get("properties",{}))}
                for entity,entries in self.values.get("entities",{}).items() for value in entries if Path(value.get("source","")).suffix.lower()==".lua"]
    def MergeLuaSceneBindings(self,bindings)->None:
        changed=False
        for binding in bindings:
            if Path(binding.get("source","")).suffix.lower()!=".lua":continue
            entries=self.values.setdefault("entities",{}).setdefault(binding["entity"],[])
            if any(value.get("type")==binding["type"] for value in entries):continue
            entries.append({key:binding[key] for key in ("source","type","enabled","properties")});changed=True
        if changed:self.Save()

def _PropertyDefault(prop):
    kind=prop.type.replace("Bazzalt::","").strip()
    if kind in ("float","double"):
        value=_Literal(prop.default)
        if isinstance(value,(int,float)):return float(value)
    if kind in ("Material","Shader","Texture","EntityReference","Entity"):return "00000000-0000-0000-0000-000000000000"
    if kind in ("Vec2","Vec3","Vec4","Quaternion"):
        values=prop.default.split(',')
        if len(values)=={"Vec2":2,"Vec3":3,"Vec4":4,"Quaternion":4}[kind]:
            try:return [float(v) for v in values]
            except ValueError:pass
    return _Literal(prop.default)

def _Literal(value: str):
    value=value.strip()
    if value in ("true","false"):return value=="true"
    if len(value)>=2 and value[0]==value[-1] and value[0] in "\"'":return value[1:-1]
    numeric=value[:-1] if value.lower().endswith("f") else value
    try:return float(numeric) if any(c in numeric.lower() for c in (".","e")) else int(numeric)
    except ValueError:return value
