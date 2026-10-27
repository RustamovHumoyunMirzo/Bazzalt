"""Extension compilation/deployment using a supplied SDK, never an engine build."""
from __future__ import annotations
import ctypes,hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
import yaml
from bazzalt.extensions import (ExtensionError,ExtensionStore,ReadManifest,Validate,AssetPaths,Local,AtomicJson,BuildPackage,CheckCompatibility,NATIVE_DIRS)

def StoreRoot():
    base=Path(os.environ.get('LOCALAPPDATA',Path.home()/'AppData/Local')) if os.name=='nt' else Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))
    return base/'BAZZALT/data/Extensions'

def EngineVersion():return json.loads((Path(__file__).resolve().parents[1]/'releases/versions.json').read_text(encoding='utf-8-sig'))['core']

def LuaLibrary(runtime):
    runtime=Path(runtime)
    names=('bazzalt_lua.dll',) if os.name=='nt' else ('libbazzalt_lua.so','libbazzalt_lua.so.5.4')
    # Bytecode is target word-size independent, but the compiler library must
    # execute on this host. Prefer the host editor's matching shared Lua.
    roots=(Path(sys.executable).parent,Path(__file__).resolve().parents[1]/'build/Editor/Release',Path(__file__).resolve().parents[1]/'build/Editor',runtime)
    for root in roots:
        for name in names:
            if (root/name).is_file():
                try:return ctypes.CDLL(str(root/name))
                except OSError:continue
    raise ExtensionError('Matching host Lua compiler library is missing')

def CompileLua(source,output,runtime):
    data=Path(source).read_bytes()
    if len(data)>4*1024*1024:raise ExtensionError('Lua module exceeds 4 MiB')
    lib=LuaLibrary(runtime);lib.luaL_newstate.restype=ctypes.c_void_p;lib.lua_close.argtypes=[ctypes.c_void_p]
    lib.luaL_loadbufferx.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_size_t,ctypes.c_char_p,ctypes.c_char_p];lib.luaL_loadbufferx.restype=ctypes.c_int
    lib.lua_tolstring.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.POINTER(ctypes.c_size_t)];lib.lua_tolstring.restype=ctypes.c_char_p
    writer_type=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p)
    lib.lua_dump.argtypes=[ctypes.c_void_p,writer_type,ctypes.c_void_p,ctypes.c_int];lib.lua_dump.restype=ctypes.c_int
    state=lib.luaL_newstate()
    if not state:raise ExtensionError('Could not allocate Lua compiler')
    parts=[];written=0
    @writer_type
    def writer(state,pointer,size,context):
        nonlocal written
        if written+size>4*1024*1024:return 1
        written+=size
        parts.append(ctypes.string_at(pointer,size));return 0
    try:
        if lib.luaL_loadbufferx(state,data,len(data),b'@Extension',b't'):raise ExtensionError((lib.lua_tolstring(state,-1,None) or b'Invalid Lua source').decode('utf-8',errors='replace'))
        if lib.lua_dump(state,writer,None,1):raise ExtensionError('Lua bytecode exceeds limit')
        output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(b''.join(parts));return output
    finally:lib.lua_close(state)

def CompileNative(source,output,runtime,architecture,compiler_path='',include_dirs=(),library_dirs=(),libraries=()):
    from Editor.scripting import ScriptCompiler
    helper=ScriptCompiler(Path(source).parent,app_root=runtime,tools={'sdk_path':str(Path(runtime)/'ScriptSDK'),'compiler_path':compiler_path},architecture=architecture)
    name='clang++.exe' if os.name=='nt' else 'clang++'
    candidates=[StoreRoot().parent/'Tools/llvm/bin'/name,Path(__file__).resolve().parents[1]/'toolchain/llvm/bin'/name]
    found=shutil.which(name)
    compiler=Path(compiler_path) if compiler_path else next((p for p in candidates if p.is_file()),Path(found) if found else None)
    if compiler and compiler.is_dir():compiler=compiler/name if (compiler/name).is_file() else compiler/'bin'/name
    sdk=helper._sdk();library=helper._link_library(sdk) if sdk else None
    if not compiler or not library:raise ExtensionError('A matching compiler and public ScriptSDK are required for native extensions')
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    command=[str(compiler),'-std=c++20','-shared','-fvisibility=hidden','-I'+str(sdk/'include'),*('-I'+str(p) for p in include_dirs),str(source),str(library),*('-L'+str(p) for p in library_dirs),*map(str,libraries),'-o',str(output)]
    if os.name=='nt':command+=['--target='+('i686' if architecture=='x86' else 'x86_64')+'-pc-windows-msvc','-fms-runtime-lib=dll','-D_ITERATOR_DEBUG_LEVEL=0']
    else:
        command+=['-fPIC','-stdlib='+('libstdc++' if architecture=='x86' else 'libc++'),'-Wl,--no-undefined','-Wl,-rpath,$ORIGIN']
        if architecture=='x86':command+=['-m32','-msse2','-mfpmath=sse']
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    if result.returncode:raise ExtensionError((result.stdout+'\n'+result.stderr)[-16000:])
    return output

def BuildExtensionPackage(source,destination,runtime,platform,architecture,compiler_path='',*,progress=lambda text:None,cancel=lambda:False):
    source=Path(source).resolve();manifest=ReadManifest(source);CheckCompatibility(manifest,EngineVersion(),platform.title(),'x86_64' if architecture=='x64' else 'x86')
    if platform!=('windows' if os.name=='nt' else 'linux') and any(Path(AssetPaths(manifest)[s['Id']]).suffix=='.cpp' for s in manifest['Scripts'].values()):raise ExtensionError('Build native extension artifacts on their target operating system using a matching SDK')
    with tempfile.TemporaryDirectory(prefix='bazzalt-extension-') as temporary:
        root=Path(temporary)
        # Copy only explicitly registered content and native SDK folders.
        for asset in manifest['AssetRegistry']:
            path=Local(source.parent,asset['Path']);target=root/asset['Path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        if manifest['General'].get('LongDescription'):
            name=manifest['General']['LongDescription'];target=root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(Local(source.parent,name),target)
        for key in NATIVE_DIRS:
            for name in manifest['Native'].get(key,[]):
                folder=Local(source.parent,name,directory=True)
                if any(p.is_symlink() or p.is_junction() for p in folder.rglob('*')):raise ExtensionError('Linked SDK content is forbidden')
                shutil.copytree(folder,root/name,dirs_exist_ok=True)
        assets=AssetPaths(manifest)
        from Editor.scripting import ScriptCompiler
        include_validator=ScriptCompiler(root)
        for registration in manifest['Scripts'].values():
            authored=root/assets[registration['Id']]
            if authored.suffix=='.cpp':include_validator._ValidateIncludes(authored)
        for name,script in manifest['Scripts'].items():
            if cancel():raise ExtensionError('Package build cancelled')
            progress(name);path=root/assets[script['Id']]
            # Component behaviors retain their source and use the normal
            # component compiler; public native modules use ExtensionRegistry.
            component=any(v['Script']==name for v in manifest.get('Extends',{}).get('Component',{}).values())
            key='portable' if path.suffix=='.lua' else platform+'-'+architecture
            output=Path('compiled')/key/(name+('.blua' if path.suffix=='.lua' else '.dll' if platform=='windows' else '.so'))
            if path.suffix=='.lua':CompileLua(path,root/output,runtime)
            else:
                native=manifest['Native'];includes=[root/p for p in native.get('IncludeDirs',[])];libdirs=[root/p for p in native.get('LibDirsWin' if platform=='windows' else 'LibDirsLinux',[])]
                if script.get('Scope')=='Editor':libdirs.extend(root/p for p in native.get('EditorLibDirsWin' if platform=='windows' else 'EditorLibDirsLinux',[]))
                compile_source=path
                if component:
                    from Editor.scripting import ScriptCompiler
                    descriptor=ScriptCompiler.Inspect(path)
                    if not descriptor or descriptor.is_subsystem:raise ExtensionError('Extension components must declare COMPONENT behaviors')
                    compile_source=root/('wrapper_'+name+'.cpp');compile_source.write_text(ScriptCompiler._Wrapper(descriptor),encoding='utf-8')
                CompileNative(compile_source,root/output,runtime,architecture,compiler_path,includes,libdirs,list(dict.fromkeys(p for d in libdirs for pattern in (('*.lib',) if platform=='windows' else ('*.so','*.so.*','*.a')) for p in d.glob(pattern))))
            script.setdefault('Compiled',{})[key]={'Path':output.as_posix(),'EngineVersion':EngineVersion()}
            manifest['AssetRegistry'].append({'Id':'Compiled_'+name,'Path':output.as_posix()})
        manifest_path=root/'extension.yaml';manifest_path.write_text(yaml.safe_dump(manifest,sort_keys=False,allow_unicode=True),encoding='utf-8')
        return BuildPackage(manifest_path,destination,progress=progress,cancel=cancel)

def RuntimeAssets(manifest):
    excluded={s['Id'] for s in manifest['Scripts'].values()}
    excluded.update(a['Id'] for a in manifest['AssetRegistry'] if a['Id'].startswith('Compiled_'))
    return [a for a in manifest['AssetRegistry'] if a['Id'] not in excluded and a.get('Scope','Runtime')=='Runtime' and Path(a['Path']).suffix.lower() not in {'.cpp','.hpp','.h','.lua','.blua','.ui','.dll','.so','.lib','.a'}]

def AssetUUID(identity,asset):
    import uuid
    return str(uuid.uuid5(uuid.NAMESPACE_URL,'bazzalt-extension:'+identity+':'+asset))

def MaterializeAssets(project,items):
    project=Path(project);directory=project.parent if project.suffix=='.bproject' else project;assets=directory/'Assets'
    if project.suffix=='.bproject':
        data=yaml.safe_load(project.read_text(encoding='utf-8-sig')) or {};assets=directory/data.get('AssetDirectory','Assets')
    if not assets.resolve().is_relative_to(directory.resolve()) or assets.resolve()==directory.resolve():raise ExtensionError('Invalid project asset directory')
    ownership=directory/'.bazzalt/Extensions/assets.json'
    if ownership.exists() and ownership.stat().st_size>4*1024*1024:raise ExtensionError('Extension asset ownership data exceeds limit')
    owned=json.loads(ownership.read_text(encoding='utf-8')) if ownership.exists() else {}
    if not isinstance(owned,dict):raise ExtensionError('Invalid extension asset ownership data')
    def restore(source,target):
        if not target.resolve().is_relative_to(assets.resolve()):raise ExtensionError('Extension restore target escapes project assets')
        key=target.relative_to(directory).as_posix();data=source.read_bytes();new=hashlib.sha256(data).hexdigest()
        if target.exists():
            current=hashlib.sha256(target.read_bytes()).hexdigest()
            if current!=new and owned.get(key)!=current:raise ExtensionError('Extension project asset changed; resolve it before updating: '+str(target))
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or target.read_bytes()!=data:
            with tempfile.NamedTemporaryFile(dir=target.parent,delete=False) as file:file.write(data);temporary=Path(file.name)
            try:os.replace(temporary,target)
            finally:temporary.unlink(missing_ok=True)
        owned[key]=new
    for item in items:
        manifest=item['Manifest'];paths=AssetPaths(manifest);folder=assets/'Extensions'/item['Id']
        component_assets=[{'Id':manifest['Scripts'][c['Script']]['Id'],'Path':paths[manifest['Scripts'][c['Script']]['Id']]} for c in manifest['Extends'].get('Component',{}).values()]
        for asset in [*RuntimeAssets(manifest),*component_assets]:
            source=Local(item['Root'],asset['Path']);target=folder/asset['Path']
            if target.is_symlink() or target.is_junction():raise ExtensionError('Linked extension asset target is forbidden')
            restore(source,target);meta=target.with_name(target.name+'.meta');expected=AssetUUID(item['Id'],asset['Id'])
            if meta.exists():
                if (yaml.safe_load(meta.read_text(encoding='utf-8-sig')) or {}).get('UUID')!=expected:raise ExtensionError('Extension asset UUID collision: '+str(target))
            else:meta.write_text(yaml.safe_dump({'FormatVersion':1,'UUID':expected,'Importer':'','ImporterVersion':0}),encoding='utf-8')
        for name in manifest['Native'].get('IncludeDirs',[]):
            source=Local(item['Root'],name,directory=True);target=folder/name
            for path in source.rglob('*'):
                if path.is_symlink() or path.is_junction():raise ExtensionError('Linked extension header is forbidden')
                if path.is_file():
                    out=target/path.relative_to(source);restore(path,out)
    AtomicJson(ownership,owned)
    return assets

def RuntimeIndex(items,destination,platform,architecture,*,editor=False,runtime=None,compiler_path=""):
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True);modules=[];names=set();native_files=[];native_dirs=set();native_names={}
    for item in items:
        manifest=item['Manifest'];identity=item['Id'];assets=AssetPaths(manifest);CheckCompatibility(manifest,EngineVersion(),platform.title(),'x86_64' if architecture=='x64' else 'x86')
        for key,script in manifest['Scripts'].items():
            if not editor and script.get('Scope')=='Editor':continue
            if any(v['Script']==key for v in manifest['Extends'].get('Component',{}).values()):continue
            path=assets[script['Id']];language='lua' if path.endswith('.lua') else 'cpp'
            target='portable' if language=='lua' else platform+'-'+architecture;compiled=script.get('Compiled',{}).get(target)
            name=script['ModuleName'] if script.get('Public') and script.get('SelfContained',True) else identity+'.'+script['ModuleName']
            if name in names:raise ExtensionError('Extension module collision: '+name)
            names.add(name)
            relative=Path(identity)/(Path('compiled')/target/(key+('.blua' if language=='lua' else '.dll' if platform=='windows' else '.so')));out=destination/relative;out.parent.mkdir(parents=True,exist_ok=True)
            if language=='cpp' and runtime is not None and (not compiled or compiled.get('EngineVersion')!=EngineVersion()):
                native=manifest['Native'];includes=[item['Root']/p for p in native.get('IncludeDirs',[])];libdirs=[item['Root']/p for p in native.get('LibDirsWin' if platform=='windows' else 'LibDirsLinux',[])]
                CompileNative(Local(item['Root'],path),out,runtime,architecture,compiler_path,includes,libdirs,list(dict.fromkeys(p for d in libdirs for pattern in (('*.lib',) if platform=='windows' else ('*.so','*.so.*','*.a')) for p in d.glob(pattern))))
            else:
                if not compiled:raise ExtensionError('Extension '+identity+' has no compiled '+target+' module: '+key)
                if language=='cpp' and compiled.get('EngineVersion')!=EngineVersion():raise ExtensionError('Native extension SDK version mismatch: '+identity)
                source_file=Local(item['Root'],compiled['Path'])
                if not out.exists() or out.read_bytes()!=source_file.read_bytes():shutil.copy2(source_file,out)
            modules.append({'Name':name,'Path':relative.as_posix(),'Language':language,'Owner':identity,'Public':bool(script.get('Public'))})
        native=manifest['Native']
        if native.get('Enabled') and (editor or native.get('Scope','Runtime')=='Runtime'):
            bins=list(native.get('BinDirsWin' if platform=='windows' else 'BinDirsLinux',[]))
            if editor:bins.extend(native.get('EditorBinDirsWin' if platform=='windows' else 'EditorBinDirsLinux',[]))
            for directory in bins:
                for source in Local(item['Root'],directory,directory=True).rglob('*'):
                    if source.is_file():
                        # Place dependent binaries next to their compiled modules.
                        target=destination/identity/'compiled'/(platform+'-'+architecture)/source.relative_to(Local(item['Root'],directory,directory=True));target.parent.mkdir(parents=True,exist_ok=True)
                        if not editor and (source.name.lower().startswith(('python','libpython','pyside','libpyside','qt6','libqt6','bazzalthub','_bazzalt')) or source.suffix.lower() in {'.py','.pyc','.ui','.pyd'}):raise ExtensionError('Editor/Python native dependencies cannot enter game SDK output: '+source.name)
                        if target.exists() and target.read_bytes()!=source.read_bytes():raise ExtensionError('Duplicate native binary: '+source.name)
                        if not target.exists():shutil.copy2(source,target)
                        if source.suffix.lower()=='.dll' or source.name.endswith('.so') or '.so.' in source.name:
                            digest=hashlib.sha256(source.read_bytes()).hexdigest();name=source.name.casefold()
                            if name in native_names and native_names[name]!=digest:raise ExtensionError('Conflicting native library basenames: '+source.name)
                            native_names[name]=digest;native_files.append(target.relative_to(destination).as_posix());native_dirs.add(target.parent.relative_to(destination).as_posix())
    from Editor.scripting import ScriptCompiler
    components={}
    for item in items:
        manifest=item['Manifest'];assets=AssetPaths(manifest)
        for component in manifest['Extends'].get('Component',{}).values():
            descriptor=ScriptCompiler.Inspect(Local(item['Root'],assets[manifest['Scripts'][component['Script']]['Id']]))
            if descriptor:components[descriptor.name]=item['Id']
    AtomicJson(destination/'runtime.json',{'FormatVersion':1,'Modules':modules,'NativeLibraries':native_files,'NativeDirectories':sorted(native_dirs),'Components':components,'Assets':{i['Id']+':'+a['Id']:AssetUUID(i['Id'],a['Id']) for i in items for a in RuntimeAssets(i['Manifest'])}})
    return destination

def LockProject(project,items):
    project=Path(project);project=project.parent if project.suffix=='.bproject' else project
    AtomicJson(project/'Extensions.lock.json',{'FormatVersion':1,'Extensions':[{'Id':i['Id'],'Version':i['Manifest']['General']['Version'],'Digest':i['Digest']} for i in items]})

def LockedItems(project,platform,architecture):
    project=Path(project);project=project.parent if project.suffix=='.bproject' else project;lock=project/'Extensions.lock.json'
    if not lock.exists():return []
    value=json.loads(lock.read_text(encoding='utf-8'))
    if value.get('FormatVersion')!=1:raise ExtensionError('Unsupported extension lock format')
    import re
    store=ExtensionStore(StoreRoot(),EngineVersion(),platform.title(),'x86_64' if architecture=='x64' else 'x86');items=[]
    for pin in value['Extensions']:
        if not re.fullmatch('[0-9a-f]{64}',pin['Digest']):raise ExtensionError('Invalid extension package digest')
        root=store.Root/'packages'/pin['Digest']
        if not root.is_dir():raise ExtensionError('Install the exact pinned .bpack for '+pin['Id'])
        manifest=Validate(json.loads(Local(root,'extension.json').read_text(encoding='utf-8')),root)
        if manifest['General']['Id']!=pin['Id'] or manifest['General']['Version']!=pin['Version']:raise ExtensionError('Extension pin metadata mismatch')
        items.append({'Id':pin['Id'],'Digest':pin['Digest'],'Root':root,'Manifest':manifest,'Enabled':True})
    return store.Resolve([i['Id'] for i in items],installed=items)

def StageGame(project,cooked,platform,architecture,runtime=None,compiler_path=""):
    items=LockedItems(project,platform,architecture)
    if items:RuntimeIndex(items,Path(cooked)/'ext',platform,architecture,runtime=runtime,compiler_path=compiler_path)
    return items


def main():
    import argparse
    parser=argparse.ArgumentParser(description='Build a self-contained .bpack using a supplied prebuilt SDK')
    parser.add_argument('--manifest',required=True);parser.add_argument('--output',required=True);parser.add_argument('--runtime',required=True);parser.add_argument('--platform',choices=('windows','linux'),required=True);parser.add_argument('--architecture',choices=('x64','x86'),default='x64');parser.add_argument('--compiler',default='')
    args=parser.parse_args()
    try:print(BuildExtensionPackage(args.manifest,args.output,args.runtime,args.platform,args.architecture,args.compiler));return 0
    except (OSError,ValueError) as error:print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
