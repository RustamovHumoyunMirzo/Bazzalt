"""Editor-owned C++ behavior discovery and compilation."""
from __future__ import annotations
from dataclasses import dataclass, field
import hashlib, json, os, re, subprocess
from pathlib import Path

_COMPONENT=re.compile(r"\bCOMPONENT\s*\(\s*([A-Za-z_]\w*)\s*\)")
_PROPERTY=re.compile(r"\bPROPERTY\s*\(\s*([^,]+?)\s*,\s*([A-Za-z_]\w*)\s*,\s*([^,\)]+)(?:,([^\)]*))?\)")

@dataclass(frozen=True)
class ScriptProperty:
    type: str; name: str; default: str; attributes: tuple[str,...]=()
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
    """Incremental compiler using only the toolchain shipped beside the editor."""
    def __init__(self, project: str|Path, engine_root: str|Path|None=None, app_root: str|Path|None=None):
        self.project=Path(project).resolve();self.engine_root=Path(engine_root or Path(__file__).resolve().parents[1])
        self.app_root=Path(app_root or Path(os.path.abspath(os.path.dirname(os.sys.executable))))
        self.cache=self.project/".bazzalt"/"ScriptAssemblies";self.state_file=self.cache/"build-state.json"
    @staticmethod
    def Inspect(path: str|Path)->ScriptDescriptor|None:
        path=Path(path)
        try:text=path.read_text(encoding="utf-8")
        except (OSError,UnicodeError):return None
        match=_COMPONENT.search(text)
        if not match:return None
        properties=tuple(ScriptProperty(m.group(1).strip(),m.group(2),m.group(3).strip(),tuple(a.strip() for a in (m.group(4) or "").split(",") if a.strip())) for m in _PROPERTY.finditer(text))
        return ScriptDescriptor(path.resolve(),match.group(1),properties)
    def Discover(self)->list[ScriptDescriptor]:
        assets=self.project/"Assets"
        return [value for path in assets.rglob("*.cpp") if (value:=self.Inspect(path)) is not None] if assets.is_dir() else []
    def _compiler(self)->Path|None:
        name="clang++.exe" if os.name=="nt" else "clang++"
        for root in (self.app_root,self.engine_root):
            candidate=root/"toolchain"/"llvm"/"bin"/name
            if candidate.is_file():return candidate
        return None
    def Build(self, used: list[str|Path])->BuildResult:
        descriptors=[value for path in dict.fromkeys(map(str,used)) if (value:=self.Inspect(path)) is not None]
        if not descriptors:return BuildResult(True)
        compiler=self._compiler()
        if compiler is None:return BuildResult(False,diagnostics=(Diagnostic("error","Bundled LLVM/Clang toolchain is missing. Repair this editor installation.",localization_key="scripting.toolchain_missing"),))
        self.cache.mkdir(parents=True,exist_ok=True)
        try:state=json.loads(self.state_file.read_text(encoding="utf-8"))
        except (OSError,json.JSONDecodeError):state={}
        outputs=[];diagnostics=[]
        for descriptor in descriptors:
            digest=hashlib.sha256(descriptor.path.read_bytes()+b"\0bazzalt-script-abi-1-wrapper-3-time-1"+(self.engine_root/"include/Bazzalt/Time.h").read_bytes()).hexdigest()
            suffix=".dll" if os.name=="nt" else ".dylib" if os.sys.platform=="darwin" else ".so"
            output=self.cache/f"{descriptor.name}-{digest[:12]}{suffix}";outputs.append(output)
            if state.get(str(descriptor.path))==digest and output.exists():continue
            wrapper=self.cache/f"{descriptor.name}-{digest[:12]}.module.cpp"
            wrapper.write_text(self._Wrapper(descriptor),encoding="utf-8")
            command=[str(compiler),"-std=c++20","-shared","-fvisibility=hidden",f"-I{self.engine_root/'include'}",str(wrapper),"-o",str(output)]
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
            elif kind in ("Vec2","Bazzalt::Vec2"):body=f'return std::sscanf(value,"%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y)==2;'
            elif kind in ("Vec3","Bazzalt::Vec3"):body=f'return std::sscanf(value,"%f,%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y,&self->{prop.name}.Z)==3;'
            elif kind in ("Vec4","Bazzalt::Vec4"):body=f'return std::sscanf(value,"%f,%f,%f,%f",&self->{prop.name}.X,&self->{prop.name}.Y,&self->{prop.name}.Z,&self->{prop.name}.W)==4;'
            else:body=f'{{std::istringstream input(value);input>>self->{prop.name};return !input.fail();}}'
            setters.append(f'if(std::strcmp(name,"{prop.name}")==0){{{body}}}')
        source=str(descriptor.path).replace("\\","/").replace('"','\\"')
        name=descriptor.name;setter="".join(setters)
        wrapper=f'''#include <Bazzalt/Script.h>\n#include <cstdio>\n#include <cstring>\n#include <sstream>\n#include <string>\n#include "{source}"\n#if defined(_WIN32)\n#define BAZZALT_SCRIPT_EXPORT __declspec(dllexport)\n#else\n#define BAZZALT_SCRIPT_EXPORT __attribute__((visibility("default")))\n#endif\nnamespace {{\nvoid* Create(){{return new {name}();}}\nvoid Destroy(void* p){{delete static_cast<{name}*>(p);}}\nvoid OnCreate(void* p){{static_cast<{name}*>(p)->OnCreate();}}\nvoid OnUpdate(void* p,float dt){{static_cast<{name}*>(p)->OnUpdate(dt);}}\nvoid OnDestroy(void* p){{static_cast<{name}*>(p)->OnDestroy();}}\nbool SetProperty(void* p,const char* name,const char* value){{auto* self=static_cast<{name}*>(p);{setter}return false;}}\nconst Bazzalt::ScriptModuleApi Api{{Bazzalt::ScriptAbiVersion,"{name}",&Create,&Destroy,&OnCreate,&OnUpdate,&OnDestroy,&SetProperty}};\n}}\nextern "C" BAZZALT_SCRIPT_EXPORT const Bazzalt::ScriptModuleApi* BazzaltGetScriptModuleV1(){{return &Api;}}\n'''
        wrapper += '\nextern "C" BAZZALT_SCRIPT_EXPORT void BazzaltBindTimeV1(Bazzalt::Detail::TimeState* state){Bazzalt::ScriptRuntimeAccess::BindTime(state);}\n'
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
    def For(self,entity: str)->list[dict]:return self.values.setdefault("entities",{}).setdefault(entity,[])
    def Attach(self,entity: str,descriptor: ScriptDescriptor)->bool:
        entries=self.For(entity)
        if any(Path(v["source"]).resolve()==descriptor.path for v in entries):return False
        entries.append({"source":str(descriptor.path),"type":descriptor.name,"enabled":True,"properties":{p.name:_Literal(p.default) for p in descriptor.properties}});self.Save();return True
    def Remove(self,entity: str,type_name: str)->bool:
        entries=self.For(entity);remaining=[v for v in entries if v.get("type")!=type_name]
        if len(remaining)==len(entries):return False
        self.values["entities"][entity]=remaining;self.Save();return True
    def UsedSources(self)->list[str]:return list(dict.fromkeys(v.get("source","") for entries in self.values.get("entities",{}).values() for v in entries if v.get("enabled",True) and v.get("source")))
    def RuntimeBindings(self,outputs: tuple[Path,...])->list[dict]:
        sources=self.UsedSources();modules={str(Path(source).resolve()):str(output) for source,output in zip(sources,outputs)};result=[]
        for entity,entries in self.values.get("entities",{}).items():
            for value in entries:
                source=str(Path(value.get("source","")).resolve())
                if value.get("enabled",True) and source in modules:result.append({"module":modules[source],"entity":entity,"type":value.get("type",Path(source).stem),"properties":dict(value.get("properties",{}))})
        return result

def _Literal(value: str):
    value=value.strip()
    if value in ("true","false"):return value=="true"
    if len(value)>=2 and value[0]==value[-1] and value[0] in "\"'":return value[1:-1]
    numeric=value[:-1] if value.lower().endswith("f") else value
    try:return float(numeric) if any(c in numeric.lower() for c in (".","e")) else int(numeric)
    except ValueError:return value
