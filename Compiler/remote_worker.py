"""SSH worker protocol. Runs only in the user-installed game compiler."""
import hashlib
import json
import os
from pathlib import Path,PurePosixPath
import shutil
import subprocess
import sys
import zipfile
from bazzalt.platforms import HostPlatform,HostArchitecture
from .pak import SafeName

PROTOCOL=1
MAX_INPUT=32*1024**3

def ToolsRoot():
    name='BazzaltCooker.exe' if HostPlatform()=='windows' else 'BazzaltCooker'
    candidates=(Path(sys.executable).parent,Path(__file__).resolve().parents[1]/('build/Editor/Release' if HostPlatform()=='windows' else 'build/Editor'))
    return next((p for p in candidates if (p/name).is_file()),candidates[0])

def Info(runtime=None,architecture=None,compiler_path=""):
    cooker=ToolsRoot()/('BazzaltCooker.exe' if HostPlatform()=='windows' else 'BazzaltCooker');ready=False;error=''
    if cooker.is_file():
        try:
            from .extensions import EngineVersion
            result=subprocess.run([str(cooker),'--version'],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=10,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            ready=result.returncode==0 and json.loads(result.stdout)['core_version']==EngineVersion()
        except (OSError,ValueError,KeyError,subprocess.TimeoutExpired):pass
    info=dict(protocol=PROTOCOL,platform=HostPlatform(),architecture=HostArchitecture(),python=sys.version.split()[0],cooker_ready=ready)
    architecture=architecture or HostArchitecture()
    runtime=Path(runtime) if runtime else ToolsRoot()/'RuntimeTemplates'/(HostPlatform()+'-'+architecture)
    try:
        from .build import ValidateRuntime
        ValidateRuntime(runtime,architecture,HostPlatform());info['runtime_architecture']=architecture;info['runtime_ready']=True
    except (OSError,ValueError,KeyError) as failure:info.update(runtime_ready=False,runtime_error=str(failure))
    compiler=Path(compiler_path) if compiler_path else None
    if compiler and compiler.is_dir():compiler=compiler/'bin'/('clang++.exe' if os.name=='nt' else 'clang++')
    if not compiler:
        found=shutil.which('clang++');compiler=Path(found) if found else None
        if not compiler and os.name=='nt':
            for variable,fallback in (('LOCALAPPDATA','AppData/Local'),('APPDATA','AppData/Roaming')):
                candidate=Path(os.environ.get(variable,Path.home()/fallback))/'BAZZALT/data/Tools/llvm/bin/clang++.exe'
                if candidate.is_file():compiler=candidate;break
    info['cpp_compiler_ready']=False
    if compiler and compiler.is_file():
        try:
            probe=subprocess.run([str(compiler),'--version'],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=10,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            info['cpp_compiler_ready']=probe.returncode==0;info['cpp_compiler_version']=probe.stdout.splitlines()[0] if probe.stdout else ''
        except (OSError,subprocess.TimeoutExpired):pass
    return info

def Extract(archive,root,sync_assets=True):
    root=Path(root);root.mkdir(parents=True,exist_ok=True);seen=set()
    with zipfile.ZipFile(archive) as source:
        entries=source.infolist()
        if len(entries)>500000 or sum(e.file_size for e in entries)>MAX_INPUT:raise ValueError('Remote project exceeds transfer limits')
        for entry in entries:
            name=entry.filename;SafeName(name);identity=name.casefold()
            if identity in seen or (entry.external_attr>>16)&0o170000==0o120000:raise ValueError('Duplicate or linked remote project path')
            seen.add(identity);path=root.joinpath(*PurePosixPath(name).parts)
            if not path.resolve().is_relative_to(root.resolve()) or any(p.is_symlink() for p in (path,*path.parents) if p.is_relative_to(root)):raise ValueError('Remote archive escaped its workspace')
            if entry.is_dir():path.mkdir(parents=True,exist_ok=True)
            else:
                path.parent.mkdir(parents=True,exist_ok=True)
                with source.open(entry) as incoming,path.open('wb') as output:shutil.copyfileobj(incoming,output)

def Execute(request_path,cancel=None):
    from .build import Build,Emit
    from .settings import Load,Save
    path=Path(request_path).resolve()
    if path.stat().st_size>65536:raise ValueError('Remote request exceeds limits')
    request=json.loads(path.read_text(encoding='utf-8'));job=path.parent
    if request.get('protocol')!=PROTOCOL:raise ValueError('Incompatible remote worker protocol')
    identity=request['project_id']
    if len(identity)!=64 or any(c not in '0123456789abcdef' for c in identity):raise ValueError('Invalid project identity')
    base=job.parent/'projects'/identity;base.mkdir(parents=True,exist_ok=True)
    if base.is_symlink() or not base.resolve().is_relative_to(job.parent.resolve()):raise ValueError('Linked remote workspace')
    from .build import BuildLock
    with BuildLock(base):
        return _ExecuteLocked(request,job,base,identity,cancel)

def _ExecuteLocked(request,job,base,identity,cancel):
    from .build import Build,Emit
    from .settings import Load,Save
    marker=base/'.bazzalt-remote-workspace'
    if not marker.exists() and any(p.name!='.bazzalt' for p in base.iterdir()):raise ValueError('Remote workspace is not owned by Bazzalt')
    marker.write_text(identity,encoding='ascii')
    root=base/'Project'
    if base.is_symlink() or not base.resolve().is_relative_to(job.parent.resolve()):raise ValueError('Linked remote workspace')
    if request.get('clean') and root.exists():
        if root.is_symlink() or not root.resolve().is_relative_to(base.resolve()):raise ValueError('Linked remote workspace')
        shutil.rmtree(root)
    archive=job/'project.zip'
    with archive.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
    if digest!=request['sha256']:raise ValueError('Remote project transfer was corrupted')
    Extract(archive,root)
    with zipfile.ZipFile(archive) as transferred:current={e.filename for e in transferred.infolist() if not e.is_dir()}
    previous=base/'transfer-manifest.json'
    old=json.loads(previous.read_text(encoding='utf-8')) if previous.exists() else []
    # Remove only files previously uploaded by this protocol. Retain asset-only
    # paths for incremental source-only syncs; never delete caches or game output.
    retained=set(current)
    import yaml
    project_file=root.joinpath(*SafeName(request['project']).parts)
    metadata=yaml.safe_load(project_file.read_text(encoding='utf-8-sig'))
    assets=(root/metadata.get('AssetDirectory','Assets')).resolve()
    if not assets.is_relative_to(root.resolve()) or assets==root.resolve():raise ValueError('Invalid project asset directory')
    for name in old:
        path=root.joinpath(*SafeName(name).parts)
        if name in current:continue
        if not request.get('sync_assets',True) and path.resolve().is_relative_to(assets) and path.suffix.lower() not in ('.cpp','.h','.hpp','.lua','.meta','.bscene'):
            retained.add(name);continue
        if not path.resolve().is_relative_to(root.resolve()) or path.is_symlink():raise ValueError('Linked transfer path')
        path.unlink(missing_ok=True)
    previous.write_text(json.dumps(sorted(retained)),encoding='utf-8')
    relative=SafeName(request['project']);project=root.joinpath(*relative.parts)
    settings=Load(project);target=settings['target']['platform']
    if target!=HostPlatform():raise ValueError('The worker operating system does not match the selected game target')
    if request.get('runtime'):settings['platforms'][target]['runtime_directory']=request['runtime']
    settings['platforms'][target]['build_method']='native'
    settings['platforms'][target]['toolchain']='' # Client paths are not valid on the worker; use its explicit override.
    settings['packaging']['archive']=True;Save(project,settings)
    app=ToolsRoot()
    runtime=Path(settings['platforms'][target]['runtime_directory']) if settings['platforms'][target]['runtime_directory'] else app/'RuntimeTemplates'/(target+'-'+settings['platforms'][target]['architecture'])
    cooker=app/('BazzaltCooker.exe' if HostPlatform()=='windows' else 'BazzaltCooker')
    result=Build(project,runtime,cooker,compiler_path=request.get('compiler',''),cancel=cancel)
    with result.open('rb') as file:digest=hashlib.file_digest(file,'sha256').hexdigest()
    Emit('remote_complete','Remote build completed',output=str(result),sha256=digest,size=result.stat().st_size)
    if request.get('remove_intermediates'):
        cache=root/'.bazzalt/Cache/Build'
        if cache.is_dir() and not cache.is_symlink() and cache.resolve().is_relative_to(root.resolve()):shutil.rmtree(cache)
    if request.get('open_output'):
        try:
            if os.name=='nt':os.startfile(result.parent)
            else:subprocess.Popen(['open' if sys.platform=='darwin' else 'xdg-open',str(result.parent)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        except OSError as error:Emit('warning','Could not open remote output folder: '+str(error))
    archive.unlink(missing_ok=True)
    return result
