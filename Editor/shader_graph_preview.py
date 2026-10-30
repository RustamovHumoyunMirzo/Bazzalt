"""Bounded offscreen Filament previews on a dedicated worker, never editor scenes."""
import math,time,json,tempfile,os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from types import SimpleNamespace
from PySide6.QtCore import QObject,Signal,QCoreApplication,Qt
from PySide6.QtGui import QImage
from .materials import MaterialCompiler,_Write
from .model_thumbnails import _Gltf,_Obj

def Geometry(path=None):
    vertices=[];indices=[]
    def Add(point,normal,uv,color=(1.,1.,1.,1.)):
        # Shortest rotation from the tangent frame's +Z axis to the normal.
        x,y,z=normal;length=math.sqrt(x*x+y*y+z*z) or 1.;x,y,z=x/length,y/length,z/length
        q=(1.,0.,0.,0.) if z<-.999999 else (-y,x,0.,1.+z);length=math.sqrt(sum(a*a for a in q));q=tuple(a/length for a in q)
        color=tuple(color)+(1.,) if len(color)==3 else tuple(color)
        vertices.append(tuple(point)+q+tuple(uv)+color);indices.append(len(vertices)-1)
    if path:
        path=Path(path);triangles=_Obj(path,True) if path.suffix.lower()=='.obj' else _Gltf(path,True)
        if not triangles:raise ValueError('Model has no supported triangle geometry')
        points=[p for tri,_ in triangles for p,_,_,_ in tri];low=[min(p[i] for p in points) for i in range(3)];high=[max(p[i] for p in points) for i in range(3)];center=[(low[i]+high[i])*.5 for i in range(3)];scale=max(high[i]-low[i] for i in range(3))*.5 or 1.
        for triangle,_ in triangles:
            a,b,c=[p for p,_,_,_ in triangle];u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)];normal=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            for point,n,uv,color in triangle:
                p=[(point[i]-center[i])/scale for i in range(3)];Add(p,n or normal,uv or ((p[0]+1)*.5,(p[1]+1)*.5),color)
    else:
        def Point(u,v):
            a=u*math.tau;b=v*math.pi;return (math.sin(b)*math.cos(a),math.cos(b),math.sin(b)*math.sin(a))
        for y in range(32):
            for x in range(48):
                for u,v in [(x/48,y/32),((x+1)/48,y/32),((x+1)/48,(y+1)/32),(x/48,y/32),((x+1)/48,(y+1)/32),(x/48,(y+1)/32)]:p=Point(u,v);Add(p,p,(u,v))
    return vertices,indices

class ShaderGraphPreview(QObject):
    Ready=Signal(object);Failed=Signal(str);_Done=Signal(int,object,str,object)
    def __init__(self,runtime,parent=None):
        super().__init__(parent);self.Runtime=runtime;self.Executor=ThreadPoolExecutor(max_workers=1,thread_name_prefix='ShaderGraphPreview');self.Session=None;self.Products={};self.Generation=0;self.Active=False;self.Pending=None;self.Closed=False;self._Done.connect(self.Complete)
        supported=runtime.SupportedRenderingBackends();self.Backend=next((value for value in ('metal','vulkan','opengl') if value in supported),None)
        if parent:parent.destroyed.connect(self.Close)
        if QCoreApplication.instance():QCoreApplication.instance().aboutToQuit.connect(self.Close)
    def Request(self,path,mesh=None):
        self.Generation+=1;record=self.Runtime.AssetInfo(str(path))
        if not record:self.Runtime.RefreshAssets();record=self.Runtime.AssetInfo(str(path))
        if not record:raise ValueError('Shader graph is not registered in the project')
        from .shader_graph import ParseSource,ReadSource
        source=ReadSource(path);graph=ParseSource(source)
        textures={}
        for node in graph['nodes']:
            if node['type']=='Texture' and node.get('values',{}).get('texture'):
                identity=node['values']['texture'];textures[identity]=dict(self.Runtime.AssetInfo(identity) or {})
        mesh_record=self.Runtime.AssetInfo(str(mesh)) if mesh else {};self.Pending=(self.Generation,dict(record),mesh_record.get('path'),textures,source);self.Start()
    def Start(self):
        if self.Active or not self.Pending or self.Closed:return
        request=self.Pending;self.Pending=None;self.Active=True
        def Work():
            generation,record,mesh,textures,source=request;publication=None
            try:
                # Never expose partial reflection or race the live editor's
                # material compiler. Stage a complete source snapshot privately.
                with tempfile.TemporaryDirectory(prefix='BazzaltGraph-') as folder:
                    snapshot=Path(folder)/'source.shadg';snapshot.write_bytes(source);staged=dict(record,path=str(snapshot),cache=str(Path(folder)/'shader'))
                    for suffix,data in self.Products.items():Path(staged['cache']+suffix).write_bytes(data)
                    compiler=MaterialCompiler(SimpleNamespace(AssetInfo=lambda _:staged));reflection=compiler.CompileShader(record['uuid'])
                    self.Products={suffix:Path(staged['cache']+suffix).read_bytes() for suffix in ('.filamat','.reflection.json','.translated.mat','.build.json')}
                    publication=(record,source,dict(self.Products))
                from .runtime import _LoadNativeModule
                module=_LoadNativeModule()
                if module is None or not hasattr(module,'ShaderPreview'):raise ValueError('Native shader preview is unavailable; rebuild the editor bridge')
                parameters=[]
                for p in reflection['parameters']:
                    parameter={'name':p['name'],'kind':p['kind'],'value':p['default']}
                    if p['kind']==6:
                        identity=p['default'];image=None
                        if identity!='00000000-0000-0000-0000-000000000000':
                            source=textures.get(identity,{}).get('path')
                            if not source:raise ValueError('Preview texture is missing from the project')
                            image=QImage(str(source))
                            if image.isNull():raise ValueError('Isolated preview requires a readable image texture; live render targets are evaluated in the scene')
                            if max(image.width(),image.height())>2048:image=image.scaled(2048,2048,Qt.AspectRatioMode.KeepAspectRatio)
                            image=image.convertToFormat(QImage.Format.Format_RGBA8888)
                        parameter.update(width=image.width() if image else 1,height=image.height() if image else 1,pixels=bytes(image.constBits()) if image else bytes([255]*4))
                    parameters.append(parameter)
                if self.Backend is None:raise ValueError('No supported GPU backend is available for shader preview')
                if self.Session is None:self.Session=module.ShaderPreview(self.Backend)
                vertices,indices=Geometry(mesh);pixels=self.Session.render(self.Products['.filamat'],vertices,indices,parameters,.4,.25,time.monotonic()%3600)
                image=QImage(pixels,256,256,256*4,QImage.Format.Format_RGBA8888).flipped(Qt.Orientation.Vertical).copy()
                if not self.Closed:self._Done.emit(generation,image,'',publication)
            except Exception as error:
                if not self.Closed:self._Done.emit(generation,None,str(error),publication)
        self.Executor.submit(Work)
    def Publish(self,publication):
        record,source,products=publication;current=self.Runtime.AssetInfo(record['uuid'])
        if not current or current.get('cache')!=record['cache'] or current.get('path')!=record['path']:return False
        from .shader_graph import ReadSource
        if ReadSource(record['path'])!=source:return False
        # Called on the UI thread, between engine ticks. Publish binary and
        # matching reflection together before the next material synchronization.
        for suffix,data in products.items():
            path=Path(record['cache']+suffix)
            if suffix!='.filamat':_Write(path,data.decode('utf-8'));continue
            if path.is_file() and path.stat().st_size==len(data) and path.read_bytes()==data:continue
            path.parent.mkdir(parents=True,exist_ok=True);name=None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as stream:name=stream.name;stream.write(data)
                os.replace(name,path)
            finally:
                if name and os.path.exists(name):os.unlink(name)
        return True
    def Complete(self,generation,image,error,publication):
        self.Active=False
        if generation==self.Generation:
            try:
                if publication and not self.Publish(publication):self.Start();return
                self.Failed.emit(error) if error else self.Ready.emit(image)
            except (OSError,ValueError) as failure:self.Failed.emit(str(failure))
        self.Start()
    def Close(self):
        if self.Closed:return
        self.Closed=True;self.Pending=None
        def Release():self.Session=None
        self.Executor.submit(Release);self.Executor.shutdown(wait=False)
