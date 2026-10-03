"""Editor-only shader reflection, Bshader translation and demand-driven matc compilation.

Source assets remain editable. Artifacts live next to the asset database cache;
neither Bshader nor matc is linked into the game runtime.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import shutil
import sys
import tempfile
import uuid

ZERO = str(uuid.UUID(int=0))
TYPES = {"float":0,"float2":1,"float3":2,"float4":3,"int":4,"bool":5,"sampler2d":6,"mat3":7,"mat4":8}
COUNTS = {0:1,1:2,2:3,3:4,4:1,5:1,6:1,7:9,8:16}

class MaterialError(ValueError):
    pass

def _ToolTemporaryRoot()->Path:
    root=Path(tempfile.gettempdir())
    if os.name=="nt" and not str(root).isascii():
        buffer=ctypes.create_unicode_buffer(32768)
        function=ctypes.WinDLL("kernel32",use_last_error=True).GetShortPathNameW
        function.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32];function.restype=ctypes.c_uint32
        length=function(str(root),buffer,len(buffer))
        if not length or length>=len(buffer) or not buffer.value.isascii():raise MaterialError("Filament tools require a writable ASCII TEMP path")
        root=Path(buffer.value)
    return root

def _Write(path:Path,text:str):
    """Replace atomically; don't invalidate watchers when contents are unchanged."""
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8")==text:return
    name=None
    try:
        with tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=path.parent,delete=False) as stream:
            name=stream.name;stream.write(text)
        os.replace(name,path)
    finally:
        if name and os.path.exists(name):os.unlink(name)

def _Tokens(source:str):
    pattern=re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|-?\d+(?:\.\d*)?(?:[eE][+-]?\d+)?|[A-Za-z_]\w*|.',re.S)
    return [m.group() for m in pattern.finditer(source) if not m.group().isspace() and not m.group().startswith(("//","/*"))]

def _Configuration(source:str)->dict:
    """Parse the declarative material block, not shader code or arbitrary Python."""
    tokens=_Tokens(source)
    try:index=tokens.index("material")+1
    except ValueError:raise MaterialError("Shader has no material configuration block") from None
    def value():
        nonlocal index
        if index>=len(tokens):raise MaterialError("Incomplete material configuration")
        token=tokens[index];index+=1
        if token in ("{","["):
            result={} if token=="{" else [];end="}" if token=="{" else "]"
            while index<len(tokens) and tokens[index]!=end:
                if tokens[index]==",":index+=1;continue
                if token=="{":
                    key=tokens[index].strip('"');index+=1
                    if index>=len(tokens) or tokens[index]!=":":raise MaterialError("Expected ':' after material key")
                    index+=1;result[key]=value()
                else:result.append(value())
            if index>=len(tokens):raise MaterialError("Unclosed material configuration")
            index+=1;return result
        if token.startswith('"'):return json.loads(token)
        if token in ("true","false"):return token=="true"
        try:return float(token) if any(c in token for c in ".eE") else int(token)
        except ValueError:return token
    result=value()
    if not isinstance(result,dict):raise MaterialError("Material configuration must be an object")
    return result

def ValidateValue(parameter:dict,value):
    kind=parameter["kind"];count=COUNTS[kind]
    if kind==6:
        try:return str(uuid.UUID(str(value))) if value else ZERO
        except ValueError:raise MaterialError("Texture reference must be an asset UUID") from None
    if kind==5:
        if not isinstance(value,bool):raise MaterialError("Expected a boolean")
        return value
    if kind==4:
        if isinstance(value,bool) or not isinstance(value,int) or not -2147483648<=value<=2147483647:raise MaterialError("Expected a signed 32-bit integer")
        return value
    values=[value] if count==1 else value
    if not isinstance(values,(list,tuple)) or len(values)!=count:raise MaterialError(f"Expected {count} numeric values")
    if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in values):raise MaterialError("Expected finite numeric values")
    return float(values[0]) if count==1 else list(map(float,values))

def Reflect(source:str)->dict:
    config=_Configuration(source);parameters=[];names=set()
    for item in config.get("parameters",[]):
        if not isinstance(item,dict):raise MaterialError("Invalid shader parameter declaration")
        name=item.get("name","");type_name=str(item.get("type","")).lower()
        if not re.fullmatch(r"[A-Za-z_]\w*",name) or name in names:raise MaterialError("Shader parameter names must be unique identifiers")
        if type_name not in TYPES:raise MaterialError(f"Unsupported shader parameter type: {type_name}")
        if int(item.get("size",1))!=1:raise MaterialError("Uniform arrays are not supported by the material Inspector yet")
        kind=TYPES[type_name];count=COUNTS[kind]
        default=ZERO if kind==6 else False if kind==5 else 0 if count==1 else [0.0]*count
        if kind in (7,8):
            side=3 if kind==7 else 4;default=[float(i//side==i%side) for i in range(count)]
        parameter={"name":name,"type":type_name,"kind":kind,"default":default}
        parameter["default"]=ValidateValue(parameter,item.get("default",default));parameters.append(parameter);names.add(name)
    return {"version":1,"name":config.get("name","Shader"),"domain":config.get("materialDomain","surface"),"requires":config.get("requires",[]),"parameters":parameters}

class MaterialCompiler:
    def __init__(self,runtime,engine_root:Path|None=None,app_root:Path|None=None):
        self.runtime=runtime;self.root=Path(engine_root or Path(__file__).resolve().parents[1]);self.app=Path(app_root or Path(sys.executable).resolve().parent)
        self._reflections={}
        self._translator=None;self._failures={}
    def _Tool(self,name:str)->Path:
        filename=name+(".exe" if os.name=="nt" else "")
        for root in (self.app,self.root):
            for directory in (root/"tools"/"filament",root/"deps"/"filament"/"bin"):
                candidate=directory/filename
                if candidate.is_file():return candidate
        raise MaterialError(f"Required editor tool is missing: {name}")
    def _Translate(self,source:str)->tuple[str,dict]:
        filename="bshad.dll" if os.name=="nt" else "libbshad.dylib" if sys.platform=="darwin" else "libbshad.so"
        roots=(self.app/"Editor",self.app,self.root/"build"/"Editor"/"Release",self.root/"build"/"Editor",self.root/"Editor")
        path=next((r/filename for r in roots if (r/filename).is_file()),None)
        if path is None:raise MaterialError("Bshader editor translator is missing; rebuild or repair the editor")
        if self._translator is None:self._translator=ctypes.CDLL(str(path))
        lib=self._translator
        lib.ShaderContextCreate.restype=ctypes.c_void_p
        lib.ShaderContextDestroy.argtypes=[ctypes.c_void_p]
        lib.ShaderTranslatorCompile.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_size_t];lib.ShaderTranslatorCompile.restype=ctypes.c_void_p
        lib.ShaderResultIsSuccess.argtypes=[ctypes.c_void_p];lib.ShaderResultIsSuccess.restype=ctypes.c_bool
        for method in ("ShaderResultGetOutput","ShaderResultGetError"):
            getattr(lib,method).argtypes=[ctypes.c_void_p];getattr(lib,method).restype=ctypes.c_char_p
        lib.ShaderCompilationResultFree.argtypes=[ctypes.c_void_p]
        context=lib.ShaderContextCreate();result=None
        if not context:raise MaterialError("Could not allocate Bshader translator context")
        try:
            encoded=source.encode("utf-8");result=lib.ShaderTranslatorCompile(context,encoded,len(encoded))
            if not result or not lib.ShaderResultIsSuccess(result):raise MaterialError((lib.ShaderResultGetError(result) or b"Bshader translation failed").decode("utf-8",errors="replace") if result else "Bshader translation failed")
            translated=lib.ShaderResultGetOutput(result).decode("utf-8")
        finally:
            if result:lib.ShaderCompilationResultFree(result)
            lib.ShaderContextDestroy(context)
        # Bshader's historical target syntax uses flat uniform identifiers and
        # editor defaults. Lower those conventions to current Filament syntax.
        reflection=Reflect(translated)
        for p in reflection["parameters"]:
            if p["kind"]!=6:translated=re.sub(r"\bmaterialParams_"+re.escape(p["name"])+r"\b","materialParams."+p["name"],translated)
        translated=re.sub(r",\s*default\s*:\s*(?:\[[^\]]*\]|[^,}\n]+)","",translated)
        split=translated.index("fragment {");configuration,fragment=translated[:split],translated[split:]
        for name,replacement in {"UV":"getUV0()","worldPosition":"getWorldPosition()","worldNormal":"getWorldNormalVector()","vertexColor":"getColor()","time":"getTime()"}.items():
            fragment=re.sub(r"(?<![.\w])\b"+name+r"\b",replacement,fragment)
        translated=configuration+fragment
        requires=[]
        if "getUV0()" in translated:requires.append("uv0")
        if "getColor()" in translated:requires.append("color")
        if requires:translated=translated.replace("shadingModel :",f"requires : [{', '.join(requires)}],\n    shadingModel :",1)
        reflection["requires"]=requires
        return translated,reflection
    def InspectShader(self,reference)->dict:
        asset=self.runtime.AssetInfo(reference)
        if not asset:raise MaterialError("Shader asset is not registered")
        source=Path(asset["path"]);suffix=source.suffix.lower()
        if suffix not in {".mat",".shad"}:raise MaterialError("Expected a .mat or .shad shader asset")
        stamp=(str(source),source.stat().st_mtime_ns,source.stat().st_size,str(asset["cache"]))
        if stamp in self._reflections and Path(str(asset["cache"])+".reflection.json").exists() and Path(str(asset["cache"])+".translated.mat").exists():return self._reflections[stamp]
        text=source.read_text(encoding="utf-8")
        if suffix==".shad":translated,reflection=self._Translate(text)
        else:translated=text;reflection=Reflect(text)
        reflection["shader"]=asset["uuid"]
        cache=Path(asset["cache"])
        _Write(Path(str(cache)+".reflection.json"),json.dumps(reflection,indent=2))
        _Write(Path(str(cache)+".translated.mat"),translated)
        self._reflections={key:value for key,value in self._reflections.items() if key[0]!=str(source)}
        self._reflections[stamp]=reflection
        return reflection
    def CompileShader(self,reference)->dict:
        reflection=self.InspectShader(reference);asset=self.runtime.AssetInfo(reference);cache=Path(asset["cache"])
        source=Path(str(cache)+".translated.mat");output=Path(str(cache)+".filamat");state=Path(str(cache)+".build.json");tool=self._Tool("matc")
        digest=hashlib.sha256(source.read_bytes()+str(tool.stat().st_mtime_ns).encode()+b"bazzalt-material-v2").hexdigest()
        try:previous=json.loads(state.read_text(encoding="utf-8"))
        except (OSError,ValueError):previous={}
        if previous.get("hash")==digest and output.is_file():return reflection
        if self._failures.get(asset["uuid"],(None,None))[0]==digest:raise MaterialError(self._failures[asset["uuid"]][1])
        temporary=Path(str(output)+".pending")
        try:
            # matc narrows Windows argv, just like cmgen. Keep all input/output
            # arguments ASCII and publish the artifact with Unicode-safe APIs.
            with tempfile.TemporaryDirectory(prefix="BazzaltMaterial-",dir=_ToolTemporaryRoot()) as staging:
                staged_source=Path(staging)/"source.mat";staged_output=Path(staging)/"compiled.filamat"
                shutil.copyfile(source,staged_source)
                process=subprocess.run([str(tool),"--platform","desktop","--api","all","--output",str(staged_output),str(staged_source)],capture_output=True,text=True,encoding="utf-8",errors="replace",creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
                if process.returncode or not staged_output.is_file():
                    message=(process.stdout+process.stderr).strip() or "Filament material compilation failed";self._failures[asset["uuid"]]=(digest,message);raise MaterialError(message)
                shutil.copyfile(staged_output,temporary)
            os.replace(temporary,output);_Write(state,json.dumps({"hash":digest,"version":1}))
            self._failures.pop(asset["uuid"],None)
        finally:
            if temporary.exists():temporary.unlink()
        return reflection
    def ReadMaterial(self,path:Path)->dict:
        data=json.loads(Path(path).read_text(encoding="utf-8"))
        if not isinstance(data,dict) or not isinstance(data.get("properties",{}),dict):raise MaterialError("Invalid material asset")
        if data.get("version",1)!=1:raise MaterialError("Unsupported material asset version")
        shader=data.get("shader") or ZERO
        try:data["shader"]=str(uuid.UUID(str(shader)))
        except ValueError:raise MaterialError("Invalid shader UUID") from None
        data.setdefault("properties",{});data.setdefault("version",1);return data
    def SaveMaterial(self,path:Path,data:dict):
        if data.get("shader") and data["shader"]!=ZERO:
            reflection=self.InspectShader(data["shader"])
            for p in reflection["parameters"]:
                if p["name"] in data["properties"]:data["properties"][p["name"]]=ValidateValue(p,data["properties"][p["name"]])
        _Write(Path(path),json.dumps(data,indent=2,ensure_ascii=False)+"\n");self.runtime.RefreshAssets()
    def PrepareMaterial(self,reference):
        asset=self.runtime.AssetInfo(reference)
        if not asset or Path(asset["path"]).suffix.lower()!=".matinst":raise MaterialError("Expected a material asset")
        data=self.ReadMaterial(Path(asset["path"]))
        if data["shader"]!=ZERO:
            reflection=self.CompileShader(data["shader"])
            for p in reflection["parameters"]:
                value=ValidateValue(p,data["properties"].get(p["name"],p["default"]))
                if p["kind"]==6 and value!=ZERO:
                    texture=self.runtime.AssetInfo(value)
                    if not texture or Path(texture["path"]).suffix.lower() not in {".png",".jpg",".jpeg",".hdr",".exr"}:raise MaterialError("Material texture reference must be a project image or HDR environment asset")
