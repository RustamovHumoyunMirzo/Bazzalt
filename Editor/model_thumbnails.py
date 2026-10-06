"""Bounded, asynchronous geometry previews; no native surfaces or scene mutations.

glTF/GLB and OBJ are drawn with an isometric camera and material base colors.
Unsupported/compressed geometry retains the normal model icon.
"""
import base64
import json
import math
import struct
import hashlib
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor, wait
from threading import Event
from pathlib import Path
from urllib.parse import unquote
from PySide6.QtCore import QObject, QPointF, Qt, Signal, Slot, QSaveFile, QIODevice, QTimer
from bazzalt.settings import DataPaths
from PySide6.QtGui import QColor, QImage, QPainter, QPolygonF

MAX_BYTES=128*1024*1024
MAX_TRIANGLES=16000
IDENTITY=(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)

def _Read(path):
    if path.stat().st_size>MAX_BYTES:raise ValueError("model preview exceeds size budget")
    return path.read_bytes()

def _Multiply(a,b):
    return tuple(sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4))

def _Matrix(node):
    if "matrix" in node:return tuple(node["matrix"])
    x,y,z,w=node.get("rotation",(0,0,0,1));sx,sy,sz=node.get("scale",(1,1,1));tx,ty,tz=node.get("translation",(0,0,0))
    return ((1-2*(y*y+z*z))*sx,2*(x*y+z*w)*sx,2*(x*z-y*w)*sx,0,
            2*(x*y-z*w)*sy,(1-2*(x*x+z*z))*sy,2*(y*z+x*w)*sy,0,
            2*(x*z+y*w)*sz,2*(y*z-x*w)*sz,(1-2*(x*x+y*y))*sz,0,tx,ty,tz,1)

def _Point(m,p):return tuple(sum(m[k*4+r]*p[k] for k in range(3))+m[12+r] for r in range(3))

def _Gltf(path):
    data=_Read(path);binary=b""
    if path.suffix.lower()==".glb":
        if len(data)<12 or struct.unpack_from("<III",data)!= (0x46546c67,2,len(data)):raise ValueError("invalid GLB")
        offset=12;document=None
        while offset+8<=len(data):
            size,kind=struct.unpack_from("<II",data,offset);offset+=8
            if offset+size>len(data):raise ValueError("invalid GLB chunk")
            chunk=data[offset:offset+size];offset+=size
            if kind==0x4e4f534a:document=json.loads(chunk)
            elif kind==0x004e4942:binary=chunk
        if document is None:raise ValueError("missing GLB document")
    else:document=json.loads(data)
    buffers=[]
    for buffer in document.get("buffers",[]):
        uri=buffer.get("uri","")
        if not uri:value=binary
        elif uri.startswith("data:"):value=base64.b64decode(uri.split(",",1)[1],validate=True)
        else:
            candidate=(path.parent/unquote(uri)).resolve()
            candidate.relative_to(path.parent.resolve()) # Never read arbitrary paths or URLs.
            value=_Read(candidate)
        if len(value)>MAX_BYTES:raise ValueError("buffer exceeds size budget")
        buffers.append(value)
    def accessor(index):
        item=document["accessors"][index]
        if "sparse" in item:raise ValueError("sparse preview accessor unsupported")
        view=document["bufferViews"][item["bufferView"]];count=item["count"]
        if not 0<=count<=1000000:raise ValueError("accessor exceeds budget")
        code={5121:"B",5123:"H",5125:"I",5126:"f"}[item["componentType"]]
        width={"SCALAR":1,"VEC3":3}[item["type"]];fmt=struct.Struct("<"+code*width)
        start=view.get("byteOffset",0)+item.get("byteOffset",0);stride=view.get("byteStride",fmt.size);raw=buffers[view["buffer"]]
        end=view.get("byteOffset",0)+view["byteLength"]
        if stride<fmt.size or start<0 or (count and start+(count-1)*stride+fmt.size>min(end,len(raw))):raise ValueError("invalid accessor bounds")
        return [fmt.unpack_from(raw,start+i*stride) for i in range(count)]
    triangles=[];mesh_cache={};nodes=document.get("nodes",[])
    primitive_count=sum(len(document["meshes"][node["mesh"]].get("primitives",[])) for node in nodes if "mesh" in node)
    primitive_budget=max(1,MAX_TRIANGLES//max(1,primitive_count))
    def mesh(index):
        if index in mesh_cache:return mesh_cache[index]
        output=[]
        for primitive in document["meshes"][index].get("primitives",[]):
            if primitive.get("mode",4)!=4 or "POSITION" not in primitive.get("attributes",{}):continue
            if any(key in primitive.get("extensions",{}) for key in ("KHR_draco_mesh_compression",)):continue
            positions=accessor(primitive["attributes"]["POSITION"])
            indices=[value[0] for value in accessor(primitive["indices"])] if "indices" in primitive else list(range(len(positions)))
            material=document.get("materials",[])[primitive["material"]] if "material" in primitive else {}
            color=material.get("pbrMetallicRoughness",{}).get("baseColorFactor",(.65,.7,.8,1))[:3]
            stride=max(1,math.ceil((len(indices)//3)/primitive_budget))*3
            for start in range(0,len(indices)-2,stride):
                output.append((tuple(positions[i] for i in indices[start:start+3]),color))
                if len(output)>=MAX_TRIANGLES:break
        mesh_cache[index]=output;return output
    def visit(index,parent,seen):
        if index in seen or len(seen)>256:return
        node=nodes[index];world=_Multiply(parent,_Matrix(node))
        if "mesh" in node:
            for points,color in mesh(node["mesh"]):
                if len(triangles)>=MAX_TRIANGLES:return
                triangles.append((tuple(_Point(world,p) for p in points),color))
        for child in node.get("children",[]):visit(child,world,seen|{index})
    scenes=document.get("scenes",[])
    children={child for node in nodes for child in node.get("children",[])}
    roots=scenes[document.get("scene",0)].get("nodes",[]) if scenes else [i for i in range(len(nodes)) if i not in children]
    for index in roots:visit(index,IDENTITY,set())
    return triangles

def _Obj(path):
    points=[];triangles=[]
    for line in _Read(path).decode("utf-8",errors="replace").splitlines():
        fields=line.split()
        if fields and fields[0]=="v":points.append(tuple(float(v) for v in fields[1:4]))
        elif fields and fields[0]=="f":
            indices=[int(v.split("/")[0]) for v in fields[1:]]
            vertices=[points[i-1 if i>0 else i] for i in indices]
            for i in range(1,len(vertices)-1):triangles.append(((vertices[0],vertices[i],vertices[i+1]),(.65,.7,.8)))
        if len(triangles)>=MAX_TRIANGLES:break
    return triangles

def RenderModelThumbnail(path,size=96):
    path=Path(path);triangles=_Obj(path) if path.suffix.lower()==".obj" else _Gltf(path)
    if not triangles:return QImage()
    def project(p):return ((p[0]-p[2])*.70710678,(p[0]+p[2])*.40824829-p[1]*.81649658,(p[0]+p[1]+p[2])*.57735027)
    projected=[(tuple(project(p) for p in points),color) for points,color in triangles]
    values=[p for points,_ in projected for p in points]
    if not all(math.isfinite(v) for p in values for v in p):raise ValueError("non-finite geometry")
    bounds=[(min(p[i] for p in values),max(p[i] for p in values)) for i in range(2)];extent=max(b-a for a,b in bounds)
    if extent<1e-9:return QImage()
    scale=(size-12)/extent;center=[(a+b)*.5 for a,b in bounds]
    image=QImage(size,size,QImage.Format.Format_ARGB32_Premultiplied);image.fill(Qt.GlobalColor.transparent)
    painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing);painter.setPen(Qt.PenStyle.NoPen)
    try:
        for points,color in sorted(projected,key=lambda item:sum(p[2] for p in item[0])):
            a,b,c=points;u=tuple(b[i]-a[i] for i in range(3));v=tuple(c[i]-a[i] for i in range(3));normal=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(n*n for n in normal))
            light=.35+.65*abs((normal[0]*.3-normal[1]*.4+normal[2]*.866)/length) if length>1e-12 else .5
            painter.setBrush(QColor.fromRgbF(*(max(0,min(1,float(channel)*light)) for channel in color)))
            painter.drawPolygon(QPolygonF([QPointF((p[0]-center[0])*scale+size/2,(p[1]-center[1])*scale+size/2) for p in points]))
    finally:painter.end()
    return image

class _WorkerPool:
    """Python-owned jobs avoid QRunnable/Shiboken wrapper lifetime dependencies."""
    def __init__(self):
        self.Closed=Event();self._executor=ThreadPoolExecutor(max_workers=1,thread_name_prefix="BazzaltThumbnail");self._jobs=[]
    def start(self,work):
        self._jobs=[job for job in self._jobs if not job.done()]
        future=self._executor.submit(work.run);self._jobs.append(future)
        return future
    def waitForDone(self,milliseconds):
        return not wait(self._jobs,timeout=milliseconds/1000).not_done
    def Close(self,*args):
        self.Closed.set();self._executor.shutdown(wait=False,cancel_futures=True)

class _Work:
    def __init__(self,key,path,cache_root):self.key=key;self.path=path;self.cache_root=cache_root
    def run(self):
        try:
            filename=self.cache_root/(hashlib.sha256(repr((2,self.key)).encode()).hexdigest()+".png")
            try:image=QImage(str(filename)) if filename.is_file() and filename.stat().st_size<1024*1024 else QImage()
            except OSError:image=QImage()
            if image.isNull():
                image=RenderModelThumbnail(self.path)
                if not image.isNull():
                    try:
                        self.cache_root.mkdir(parents=True,exist_ok=True);output=QSaveFile(str(filename))
                        if output.open(QIODevice.OpenModeFlag.WriteOnly):
                            if image.save(output,"PNG"):output.commit()
                            else:output.cancelWriting()
                    except OSError:pass # Read-only caches must not prevent previews.
        except Exception:image=QImage() # Malformed/unsupported files keep their fallback icon.
        return image

class ModelThumbnailCache(QObject):
    Ready=Signal(str,object)
    def __init__(self,parent=None,cache_root=None):
        super().__init__(parent);self._cache=OrderedDict();self._pending=set();self._futures={};self._errors={}
        self._cache_root=Path(cache_root) if cache_root is not None else DataPaths.Root()/"Cache"/"ModelThumbnails"
        self._pool=_WorkerPool()
        # Workers only return QImages. Collect futures on the owning Qt thread;
        # no worker-thread Python QObject signal/receiver wrapper is involved.
        self._completion_timer=QTimer(self);self._completion_timer.setInterval(16)
        self._completion_timer.timeout.connect(self._CollectCompleted)
        self.destroyed.connect(self._pool.Close)
    def Request(self,path):
        if self._pool.Closed.is_set():return None
        path=Path(path)
        if path.suffix.lower() not in {".glb",".gltf",".obj"}:return None
        try:
            stamp=path.stat();dependencies=[]
            if path.suffix.lower()==".gltf":
                # Include external buffer changes in the key, without parsing on the UI thread.
                dependencies=[(str(p),p.stat().st_mtime_ns,p.stat().st_size) for p in path.parent.glob("*.bin")]
            key=(str(path),stamp.st_mtime_ns,stamp.st_size,tuple(sorted(dependencies)))
        except OSError:return None
        if key in self._cache:self._cache.move_to_end(key);return self._cache[key]
        if key not in self._pending and len(self._pending)<64:
            self._pending.add(key);self._futures[key]=self._pool.start(_Work(key,path,self._cache_root))
            if not self._completion_timer.isActive():self._completion_timer.start()
        return None
    def WorkerDiagnostics(self):
        return {"closed":self._pool.Closed.is_set(),"timer_active":self._completion_timer.isActive(),
                "jobs":{repr(key):("cancelled" if future.cancelled() else "done" if future.done() else "running" if future.running() else "queued") for key,future in self._futures.items()},
                "errors":dict(self._errors)}
    @Slot()
    def _CollectCompleted(self):
        if self._pool.Closed.is_set():
            self._completion_timer.stop();self._pending.clear();self._futures.clear();return
        for key,future in list(self._futures.items()):
            if not future.done():continue
            del self._futures[key]
            try:image=future.result()
            except Exception as error:
                self._errors[key[0]]=repr(error)
                while len(self._errors)>128:self._errors.pop(next(iter(self._errors)))
                image=QImage()
            self._Finished(key,image)
        if not self._futures:self._completion_timer.stop()
    @Slot(object,object)
    def _Finished(self,key,image):
        if self._pool.Closed.is_set():return
        self._pending.discard(key);self._cache[key]=image
        while len(self._cache)>128:self._cache.popitem(last=False)
        self.Ready.emit(key[0],image)
