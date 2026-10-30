"""Content-only game builds. Consumes prebuilt runtime SDKs; never invokes CMake."""
from __future__ import annotations
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile
import threading
from .settings import Load,Validate,Effective
from .pak import Pack

class BuildCancelled(Exception):pass

def CheckCancelled(cancel):
    if cancel is not None and cancel():raise BuildCancelled('Game build cancelled before publication')

def Emit(stage,message,**fields):
    # ASCII JSON is lossless and safe through legacy Windows pipe code pages.
    print(json.dumps(dict(stage=stage,message=message,**fields),ensure_ascii=True),flush=True)
def Architecture(path):
    from scripts.release_artifacts import Architecture as Inspect
    return Inspect(path)

def ValidateCooker(path,platform,host_architecture=None):
    path=Path(path).resolve()
    if not path.is_file():raise ValueError('The content cooker tool is missing')
    with path.open('rb') as binary:magic=binary.read(4)
    expected=bytes.fromhex('cffaedfe') if platform=='macos' else b'\x7fELF' if platform=='linux' else b'MZ'
    if not magic.startswith(expected):raise ValueError('Content cooker platform mismatch')
    architecture=Architecture(path)
    # A multilib Linux host can execute the x86 cooker built with its runtime.
    # Cooking is content-only and independent of the target game's word size.
    from bazzalt.platforms import HostArchitecture
    host_architecture=host_architecture or HostArchitecture()
    supported=('x64','x86') if platform=='linux' and host_architecture=='x64' else (host_architecture,)
    if architecture not in supported:raise ValueError('Content cooker architecture is incompatible with this host')
    if platform!='windows' and not os.access(path,os.X_OK):raise ValueError('The content cooker tool is not executable')
    return path
@contextmanager
def BuildLock(project):
    directory=Path(project)/'.bazzalt/Cache/Build';directory.mkdir(parents=True,exist_ok=True)
    with (directory/'build.lock').open('a+b') as file:
        file.seek(0);file.write(b'\0');file.flush();file.seek(0)
        try:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(file.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as error:raise ValueError('Another build is already running for this project') from error
        try:yield directory
        finally:
            if os.name=='nt':file.seek(0);msvcrt.locking(file.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(file,fcntl.LOCK_UN)
def Run(command,**kwargs):
    process=subprocess.run([str(arg) for arg in command],capture_output=True,text=True,encoding='utf-8',errors='replace',creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0,**kwargs)
    if process.returncode:raise ValueError((process.stderr or process.stdout or 'Tool failed')[-12000:])
    return process

@contextmanager
def BuildWorkspace(directory):
    """Disposable cook output; incremental script cache lives outside this tree."""
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    # The caller holds BuildLock. Only reclaim explicitly owned abandoned
    # workspaces, never legacy/user folders or links into another directory.
    marker='.bazzalt-build-workspace'
    for candidate in directory.glob('work-*'):
        if candidate.is_symlink() or candidate.is_junction() or not candidate.is_dir():continue
        ownership=candidate/marker
        if ownership.is_file() and not ownership.is_symlink() and ownership.read_bytes()==b'BazzaltBuildWorkspace-v1':
            if any(p.is_symlink() or p.is_junction() for p in candidate.rglob('*')):continue
            shutil.rmtree(candidate)
    with tempfile.TemporaryDirectory(prefix='work-',dir=directory) as temporary:
        work=Path(temporary);(work/marker).write_bytes(b'BazzaltBuildWorkspace-v1')
        yield work
def ValidateRuntime(runtime,architecture,platform="windows"):
    runtime=Path(runtime).resolve()
    manifest=json.loads((runtime/'runtime.json').read_text(encoding='utf-8-sig'))
    if manifest.get('format_version')!=1 or manifest.get('architecture')!=architecture:raise ValueError('Runtime template version or architecture mismatch')
    if manifest.get('game_format_version')!=3:raise ValueError('Runtime template does not support protected version-3 manifests; install the updated matching Core runtime template')
    versions=Path(__file__).resolve().parents[1]/'releases/versions.json'
    core_version=json.loads(versions.read_text(encoding='utf-8-sig'))['core']
    if manifest.get('core_version')!=core_version:raise ValueError('Runtime template does not match this compiler/core version')
    if manifest.get('platform','windows')!=platform:raise ValueError('Runtime platform mismatch')
    required=['BazzaltPlayer.exe','Bazzalt.dll','bazzalt_lua.dll','bazzalt_gui.dll','bazzalt_physics.dll'] if platform=='windows' else ['BazzaltPlayer','libbazzalt_player.so','libBazzalt.so','libbazzalt_lua.so','libbazzalt_gui.so','libbazzalt_physics.so']
    if platform=='macos':required=['BazzaltPlayer','libbazzalt_player.dylib','libBazzalt.dylib','libbazzalt_lua.dylib','libbazzalt_gui.dylib','libbazzalt_physics.dylib']
    files=manifest.get('files',required)
    if not set(required)<=set(files):raise ValueError('Incomplete runtime template')
    for name in files:
        if Path(name).name!=name or (platform=='windows' and Path(name).suffix.lower() not in {'.exe','.dll'}):raise ValueError('Unsafe runtime manifest')
        with (runtime/name).open('rb') as binary:magic=binary.read(4)
        if (platform=='linux' and magic!=b'\x7fELF') or (platform=='windows' and magic[:2]!=b'MZ'):raise ValueError('Runtime binary platform mismatch: '+name)
        if platform=='macos':
            from .macos import Architectures
            if architecture not in Architectures(runtime/name):raise ValueError('Runtime Mach-O architecture mismatch: '+name)
        elif Architecture(runtime/name)!=architecture:raise ValueError('Runtime binary architecture mismatch: '+name)
        if name.lower().startswith(('python','libpython','pyside','libpyside','qt6','libqt6','bazzalthub','_bazzalt')):raise ValueError('Editor/Python binaries are forbidden in game output')
    return runtime,files
def KeyFor(settings):
    security=settings['security']
    if security['encryption']=='none':return None
    if security['key_storage'] in ('environment','keychain'):
        key=bytes.fromhex(os.environ.get(security['key_environment'],''))
        if len(key)!=32:raise ValueError('Set the configured key environment variable to 64 hex characters before building')
        return key
    return os.urandom(32)
def CompileScripts(project,runtime,architecture,cache,compiler_path=''):
    from Editor.scripting import ScriptCompiler,ScriptAttachments
    import yaml
    class GameScriptCompiler(ScriptCompiler):
        def _Validate(self,paths):return super()._Validate([*paths,*(p for p in assets.rglob('*') if p.is_file() and p.suffix.lower() in {'.cpp','.lua'})])
        def _compiler(self):
            from .cross import Active
            if Active.get():
                if Active.get()['method']=='content_only':raise ValueError('Content-only builds cannot compile C++ scripts or subsystems. Use a matching cross SDK/toolchain or a native remote worker.')
                return Path(Active.get()['compiler'])
            if os.name!='nt':
                if compiler_path:
                    value=Path(compiler_path);value=value/'bin/clang++' if value.is_dir() else value
                    return value if value.is_file() else None
                found=shutil.which('clang++');return Path(found) if found else None
            if compiler_path:
                value=Path(compiler_path)
                if value.is_dir():value=value/'bin/clang++.exe'
                return value if value.is_file() else None
            base=Path(os.environ.get('LOCALAPPDATA',Path.home()/'AppData/Local'))/'BAZZALT/data/Tools/llvm/bin/clang++.exe'
            candidates=(base,Path(os.environ.get('APPDATA',Path.home()/'AppData/Roaming'))/'BAZZALT/data/Tools/llvm/bin/clang++.exe',Path(__file__).resolve().parents[1]/'toolchain/llvm/bin/clang++.exe')
            return next((path for path in candidates if path.is_file()),None)
    compiler=GameScriptCompiler(project.parent,app_root=runtime,tools={'sdk_path':str(runtime/'ScriptSDK'),'compiler_path':compiler_path},architecture=architecture)
    compiler.cache=cache/'Scripts';compiler.state_file=compiler.cache/'build-state.json'
    attachments=ScriptAttachments(project.parent);sources=attachments.UsedSources()
    # Native attachments saved in scenes are authoritative when no legacy
    # editor-side attachment exists (e.g. projects copied to a build machine).
    project_data=yaml.safe_load(project.read_text(encoding='utf-8-sig'))
    if not isinstance(project_data,dict):raise ValueError('Project metadata must be a mapping')
    assets=project.parent/project_data.get('AssetDirectory','Assets')
    if not assets.resolve().is_relative_to(project.parent) or assets.resolve()==project.parent:raise ValueError('AssetDirectory must be a dedicated folder inside the project, not the project root')
    if any(assets.resolve().is_relative_to(project.parent/name) for name in ('.bazzalt','bin')):raise ValueError('AssetDirectory cannot be a build output or cache folder')
    identities={}
    for meta in assets.rglob('*.meta'):
        record=yaml.safe_load(meta.read_text(encoding='utf-8-sig')) or {}
        if isinstance(record,dict) and record.get('UUID'):identities[str(record['UUID'])]=Path(str(meta)[:-5])
    for path in assets.rglob('*.bscene'):
        scene=yaml.safe_load(path.read_text(encoding='utf-8-sig')) or {}
        for entity in scene.get('Entities') or []:
            for component in entity.get('Components') or []:
                if component.get('Type')!='Bazzalt.ScriptComponents':continue
                fields=component.get('Properties',{})
                count=int(fields.get('Count',0))
                if not 0<=count<=1024:raise ValueError('Invalid scene script count')
                for index in range(count):
                    prefix=f'Script.{index}.';source=str(fields.get(prefix+'Source',''));source=identities.get(source,Path(source));source=source if source.is_absolute() else project.parent/source
                    if source.suffix.lower()!='.cpp':continue
                    entries=attachments.values.setdefault('entities',{}).setdefault(entity['UUID'],[])
                    if any(item.get('type')==fields.get(prefix+'TypeName') for item in entries):continue
                    count_properties=int(fields.get(prefix+'Count',0))
                    if not 0<=count_properties<=1024:raise ValueError('Invalid scene property count')
                    properties={str(fields[prefix+f'Property.{i}.Name']):str(fields[prefix+f'Property.{i}.Value']) for i in range(count_properties)}
                    entries.append(dict(source=str(source.resolve()),type=str(fields[prefix+'TypeName']),enabled=str(fields.get(prefix+'Enabled',True)).lower()=='true',properties=properties))
    from Editor.subsystems import Registrations,Sources,WithSubsystems
    if (project_data.get('Properties') or {}).get('application.script'):raise ValueError('Application Script was replaced; register C++ scripts in Window > Global Subsystems')
    subsystem_sources=Sources(project,Registrations(project_data.get('Properties') or {}))
    attachments=WithSubsystems(attachments,subsystem_sources);compiler.SubsystemSources=list(subsystem_sources.values())
    sources=attachments.UsedSources()
    if any(not Path(path).resolve().is_relative_to(project.parent) for path in sources):raise ValueError('Script attachments must reference sources inside the project')
    result=compiler.Build([p for p in sources if Path(p).suffix.lower()!='.lua'])
    if not result.success:raise ValueError('\n'.join(d.message for d in result.diagnostics))
    return [binding for binding in attachments.RuntimeBindings(result.outputs) if Path(binding['module']).suffix.lower() in {'.dll','.so','.dylib'}]
def CookShaders(cooked,cooker,check=lambda:None):
    import yaml
    from Editor.materials import MaterialCompiler
    records=yaml.safe_load((cooked/'.bazzalt/CookedAssets.yaml').read_text(encoding='utf-8'))['Assets']
    by_id={value['UUID']:dict(uuid=value['UUID'],path=str(cooked/value['Source']),cache=str(cooked/value['Cache'])) for value in records}
    class Assets:
        def AssetInfo(self,reference):return by_id.get(str(reference),{})
    class ShaderCompiler(MaterialCompiler):
        def _Tool(self,name):
            path=cooker.parent/'filament'/(name+('.exe' if os.name=='nt' else ''))
            return path if path.is_file() else super()._Tool(name)
    compiler=ShaderCompiler(Assets(),app_root=cooker.parent)
    for record in records:
        check()
        if Path(record['Source']).suffix.lower() in {'.mat','.shad','.bshader'}:compiler.CompileShader(record['UUID'])
def RenameWithRetry(source,target):
    # Windows can briefly retain image/AV handles after a verifier exits.
    # Never bypass a persistent lock from a game that is still running.
    for attempt in range(12):
        try:source.rename(target);return
        except PermissionError:
            if attempt==11:raise
            time.sleep(.05*(attempt+1))

def Publish(staged,output,owner):
    marker='.bazzalt-game-build'
    if output.exists():
        if not output.is_dir() or not (output/marker).is_file() or (output/marker).read_text()!=owner:raise ValueError('Refusing to replace an output not owned by this project: '+str(output))
    (staged/marker).write_text(owner,encoding='utf-8')
    backup=output.with_name(output.name+'.previous-'+uuid.uuid4().hex)
    if output.exists():RenameWithRetry(output,backup)
    try:RenameWithRetry(staged,output)
    except OSError:
        if backup.exists():RenameWithRetry(backup,output)
        raise
    if backup.exists():shutil.rmtree(backup)
def Build(project,runtime,cooker,launch=False,compiler_path='',*,cancel=None):
    from .cross import Active,Resolve,WslCommand
    from bazzalt.platforms import HostPlatform
    settings=Validate(Load(project));profile=settings['platforms'][settings['target']['platform']]
    options=Resolve(settings,runtime)
    if options and options['method']=='wsl':
        if launch:raise ValueError('Build and Launch is unavailable for cross-platform output')
        process=subprocess.Popen(WslCommand(project,profile),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        output=None;forward_stop=threading.Event()
        def forward_cancel():
            while not forward_stop.wait(.05):
                if cancel and cancel():
                    try:process.stdin.write(b'cancel\n');process.stdin.flush()
                    except (OSError,ValueError):pass
                    return
        threading.Thread(target=forward_cancel,daemon=True).start()
        try:
            for line in process.stdout:
                try:
                    event=json.loads(line)
                    if event.get('stage')=='complete':
                        output=event.get('output');prefix=WslCommand(project,profile);prefix=prefix[:prefix.index('--')+1]
                        mapped=subprocess.run([*prefix,'wslpath','-a','-w',output],capture_output=True,encoding='utf-8',timeout=30,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                        if mapped.returncode:raise ValueError('Could not map WSL output to the host filesystem')
                        output=mapped.stdout.strip();event['output']=output
                    print(json.dumps(event,ensure_ascii=True),flush=True)
                except json.JSONDecodeError:print(line.decode('utf-8','replace').rstrip(),flush=True)
            if process.wait() or not output:raise ValueError('WSL build failed; inspect the Linux compiler output')
            return Path(output)
        finally:
            forward_stop.set()
            if process.poll() is None:process.terminate();process.wait(timeout=15)
    from .cross import CanExecute
    if launch and not CanExecute(settings['target']['platform'],settings['target']['architecture']):raise ValueError('Build and Launch requires a native target; cross output can be copied to its target machine')
    token=Active.set(options)
    try:return _Build(project,runtime,cooker,launch,compiler_path or (profile.get('toolchain','') if options is None else ''),cancel=cancel)
    finally:Active.reset(token)

def _Build(project,runtime,cooker,launch=False,compiler_path='',*,cancel=None):
    project=Path(project).resolve();settings=Effective(Validate(Load(project)));arch=settings['target']['architecture']
    if not project.is_file() or project.suffix!='.bproject':raise ValueError('Select a valid project file')
    from bazzalt.platforms import TargetError
    platform=settings['target']['platform'];error=TargetError({**settings['target'],'key_storage':settings['security']['key_storage']})
    if error:raise ValueError(error)
    suffix='.exe' if platform=='windows' else ''
    runtime,files=ValidateRuntime(runtime,arch,platform)
    from bazzalt.platforms import HostPlatform
    cooker=ValidateCooker(cooker,HostPlatform())
    with BuildLock(project.parent) as cache, BuildWorkspace(cache/(arch if platform=='windows' else platform+'-'+arch)) as work:
        check=lambda:CheckCancelled(cancel)
        check()
        cooked=work/'Cooked';cooked.mkdir()
        Emit('scripts','Compiling game scripts',architecture=arch)
        from .extensions import LockedItems,MaterializeAssets
        extension_items=LockedItems(project,platform,arch)
        if extension_items:MaterializeAssets(project,extension_items)
        bindings=CompileScripts(project,runtime,arch,cache/arch,compiler_path)
        check()
        for binding in bindings:
            binding['properties']={key:('true' if value is True else 'false' if value is False else ','.join(map(str,value)) if isinstance(value,list) else str(value)) for key,value in binding['properties'].items()}
        manifest=work/'bindings.json';manifest.write_text(json.dumps(dict(startup=settings['target']['startup_scene'],bindings=bindings),ensure_ascii=False),encoding='utf-8')
        Emit('cook','Importing and cooking game assets')
        environment=dict(os.environ)
        tools=cooker.parent/'filament'
        if tools.is_dir():environment['BAZZALT_FILAMENT_TOOLS']=str(tools)
        Run([cooker,project,cooked,manifest],cwd=cooker.parent,env=environment)
        from .content import FilterCooked
        check();CookShaders(cooked,cooker,check);check();FilterCooked(cooked,settings['packaging']);check()
        from .extensions import StageGame
        StageGame(project,cooked,platform,arch,runtime,compiler_path);check()
        # No source code, import tools, or editor settings can enter the final game.
        forbidden={'.py','.pyc','.pyd','.cpp','.hpp','.h','.cc','.cxx','.lua'}
        for path in cooked.rglob('*'):
            if path.is_file() and path.suffix.lower() in forbidden:raise ValueError('Source file leaked into cooked content: '+str(path))
        destination=project.parent/'bin';destination.mkdir(exist_ok=True)
        name=settings['production']['name'];output=destination/f'{name}-{platform}-{arch}'
        with tempfile.TemporaryDirectory(prefix='.bazzalt-build-',dir=destination) as temp:
            publication=Path(temp)/(name+'.app' if platform=='macos' else 'Game')
            if platform=='macos':
                from . import macos
                root=macos.CreateBundle(publication,settings);output=output.with_suffix('.app')
            else:root=publication;root.mkdir()
            for filename in files:shutil.copy2(runtime/filename,root/(name+suffix if filename=='BazzaltPlayer'+suffix else filename))
            from .pe_resources import Apply
            if platform=='windows':Apply(root/(name+suffix),settings['production'])
            from .security import SecurityContext
            if platform=='macos':macos.SignLibraries(cooked,settings['platforms']['macos'])
            security=SecurityContext(settings,KeyFor(settings));packages=[];content_hashes={}
            if settings['packaging']['format']=='pak':
                Emit('package','Packing game content')
                packages=[p.name for p in Pack(cooked,root,settings['packaging']['chunk_mb'],security.PackageKey,hashes=content_hashes,check=check,portable=platform!='windows')]
            else:shutil.copytree(cooked,root/'Data')
            if settings['packaging']['decoy_kb']:(root/'padding.bin').write_bytes(os.urandom(settings['packaging']['decoy_kb']*1024))
            game=dict(format_version=3,format=settings['packaging']['format'],production=settings['production'],window=settings['window'],intro=settings['intro'],packages=packages,**{k:settings['security'][k] for k in ('encryption','key_storage','key_environment')})
            (root/'game.json').write_text(json.dumps(game,ensure_ascii=False,indent=2),encoding='utf-8')
            (root/'LICENSE.txt').write_text(settings['production']['license'],encoding='utf-8')
            if (runtime/'licenses').is_dir():shutil.copytree(runtime/'licenses',root/'licenses')
            from .integrity import Seal
            if platform=='macos':
                owner=hashlib.sha256(str(project).encode('utf-8')).hexdigest();(publication/'.bazzalt-game-build').write_text(owner,encoding='utf-8')
                macos.SignLibraries(root,settings['platforms']['macos']);macos.Sign(publication,settings['platforms']['macos'])
            Seal(root,root/(name+suffix),security=security,content=content_hashes)
            if platform=='macos':macos.Finalize(publication,settings['platforms']['macos'])
            check()
            Emit('verify','Verifying the packaged game')
            verify_environment=dict(os.environ)
            if security.Method=='keychain':verify_environment[settings['security']['key_environment']]=security.Master.hex()
            from .cross import CanExecute
            if not CanExecute(platform,arch):
                from .verify import Verify
                Verify(root,root/(name+suffix),security.Master)
            else:Run([root/(name+suffix),'--verify-package'],cwd=root,timeout=120,env=verify_environment)
            check()
            if settings['packaging']['archive']:
                archive=destination/f'{name}-{platform}-{arch}.zip';temporary=work/'Game.zip'
                if platform=='macos':macos.Archive(publication,temporary)
                else:
                    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as zipped:
                        for path in root.rglob('*'):
                            check()
                            if path.is_file():zipped.write(path,path.relative_to(root).as_posix())
                # Existing archives require the compiler's companion ownership marker.
                marker=archive.with_suffix('.zip.owner');owner=hashlib.sha256(str(project).encode('utf-8')).hexdigest()
                if archive.exists() and (not marker.is_file() or marker.read_text()!=owner):raise ValueError('Archive is not owned by this project')
                check();Emit('publish','Publishing verified game output')
                os.replace(temporary,archive);marker.write_text(owner,encoding='utf-8');result=archive
                if output.is_dir() and (output/'.bazzalt-game-build').is_file() and (output/'.bazzalt-game-build').read_text()==owner:shutil.rmtree(output)
                if launch:
                    # The game outlives the disposable cook workspace. Keep one
                    # compiler-owned launch image rather than one per build.
                    launch_output=cache/(arch if platform=='windows' else platform+'-'+arch)/('Launch.app' if platform=='macos' else 'Launch')
                    Publish(publication,launch_output,owner);launch_path=launch_output/('Contents/MacOS' if platform=='macos' else '')/(name+suffix)
            else:
                check();Emit('publish','Publishing verified game output')
                Publish(publication,output,hashlib.sha256(str(project).encode('utf-8')).hexdigest());result=output
                if launch:launch_path=output/('Contents/MacOS' if platform=='macos' else '')/(name+suffix)
    # Exit both the staging context and build lock before reporting success or
    # launching. Cleanup failures must never arrive after a game was launched.
    Emit('complete','Game build completed',output=str(result))
    if launch:
        try:RunLaunch(launch_path)
        except OSError as error:Emit('launch_error','Game was built, but could not be launched: '+str(error),output=str(result))
    return result
def RunLaunch(executable):subprocess.Popen([str(executable)],cwd=executable.parent,close_fds=True,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def ReadControls(cancelled,stop):
    # Never leave a daemon holding buffered stdin's lock during shutdown.
    try:descriptor=sys.stdin.fileno()
    except (AttributeError,OSError,ValueError):return
    pending=b''
    if os.name=='nt':
        import ctypes,msvcrt
        api=ctypes.WinDLL('kernel32');api.PeekNamedPipe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint),ctypes.c_void_p];api.PeekNamedPipe.restype=ctypes.c_int
        handle=msvcrt.get_osfhandle(descriptor)
    while not stop.is_set():
        if os.name=='nt':
            available=ctypes.c_uint()
            if not api.PeekNamedPipe(handle,None,0,None,ctypes.byref(available),None):return
            size=min(available.value,4096)
        else:
            import select
            size=4096 if select.select([descriptor],[],[],.05)[0] else 0
        if size:
            try:data=os.read(descriptor,size)
            except OSError:return
            if not data:return
            pending+=data
            while b'\n' in pending:
                line,pending=pending.split(b'\n',1)
                if line.strip()==b'cancel':cancelled.set();return
            if len(pending)>4096:pending=b''
        stop.wait(.05)

def main():
    parser=argparse.ArgumentParser(description='Build game content using a prebuilt Bazzalt runtime')
    parser.add_argument('--project',type=Path);parser.add_argument('--runtime',type=Path);parser.add_argument('--cooker',type=Path);parser.add_argument('--launch',action='store_true');parser.add_argument('--script-compiler',default='');parser.add_argument('--generate-key',action='store_true',help='Print a fresh 256-bit key; treat stdout as secret')
    parser.add_argument('--remote-architecture',choices=('x64','x86','arm64'));parser.add_argument('--remote-info',action='store_true');parser.add_argument('--remote-request',type=Path)
    args=parser.parse_args()
    if args.remote_info:
        from .remote_worker import Info
        print(json.dumps(Info(args.runtime,args.remote_architecture,args.script_compiler)),flush=True);return 0
    if args.generate_key:
        if args.project or args.runtime or args.cooker or args.launch or args.script_compiler:parser.error('--generate-key cannot be combined with build arguments')
        print(os.urandom(32).hex(),flush=True);return 0
    if not args.remote_request and (not args.project or not args.runtime or not args.cooker):parser.error('--project, --runtime and --cooker are required for a build')
    cancelled=threading.Event();stop=threading.Event();controls=threading.Thread(target=ReadControls,args=(cancelled,stop),daemon=True);controls.start()
    try:
        if args.remote_request:
            from .remote_worker import Execute
            Execute(args.remote_request,cancelled.is_set)
        else:Build(args.project,args.runtime,args.cooker,args.launch,args.script_compiler,cancel=cancelled.is_set)
        return 0
    except BuildCancelled as error:Emit('cancelled',str(error));return 2
    except Exception as error:Emit('error',str(error));return 1
    finally:stop.set();controls.join(timeout=.5)
if __name__=='__main__':sys.exit(main())
