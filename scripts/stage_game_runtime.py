"""Assemble a prebuilt runtime template. Does not compile the engine."""
import argparse,json,shutil,os,subprocess
from pathlib import Path
try:from .release_artifacts import Architecture
except ImportError:from release_artifacts import Architecture

def RuntimeLibraries(folder,architecture):
    folder=Path(folder);files=[]
    for file in sorted(folder.glob('*.dll')):
        actual=Architecture(file)
        # Microsoft's Arm64 redist also carries this x64-only exception-handler
        # shim for emulated x64 programs. It is not a native Arm64 dependency.
        if architecture=='arm64' and actual=='x64' and file.name.lower()=='vcruntime140_1.dll':continue
        if actual!=architecture:raise ValueError(f'Wrong VC runtime architecture: {file.name} is {actual}, expected {architecture}')
        files.append(file)
    if not {'msvcp140.dll','vcruntime140.dll'}.issubset({file.name.lower() for file in files}):raise ValueError('Required native Microsoft C++ runtime libraries are missing')
    return files

def Stage(build,output,architecture,root=None,platform=None):
    from bazzalt.platforms import HostPlatform
    platform=platform or HostPlatform();linux=platform!="windows"
    build=Path(build).resolve();output=Path(output).resolve();root=Path(root or Path(__file__).resolve().parents[1]);native=build/('Editor' if linux else 'Editor/Release')
    output.mkdir(parents=True,exist_ok=True)
    names=['BazzaltPlayer.exe','Bazzalt.dll','bazzalt_lua.dll','bazzalt_gui.dll','bazzalt_physics.dll'] if not linux else ['BazzaltPlayer','libbazzalt_player.so','libBazzalt.so','libbazzalt_lua.so','libbazzalt_gui.so','libbazzalt_physics.so']
    if platform=="macos":names=["BazzaltPlayer","libbazzalt_player.dylib","libBazzalt.dylib","libbazzalt_lua.dylib","libbazzalt_gui.dylib","libbazzalt_physics.dylib"]+sorted(p.name for p in native.glob("libbazzalt_lua.*.dylib"))
    if platform=="linux":
        names+=sorted(p.name for p in native.glob('libbazzalt_lua.so.*'))
    for name in names:
        if Architecture(native/name)!=architecture:raise ValueError('Wrong runtime architecture: '+name)
        shutil.copy2(native/name,output/name)
    # App-local Microsoft CRT dependencies are required on clean consumer PCs.
    for file in (build/'ScriptSDK/lib').glob('*.dll'):
        if file.name not in names:
            if Architecture(file)!=architecture:raise ValueError('Wrong CRT architecture')
            shutil.copy2(file,output/file.name);names.append(file.name)
    if os.name=='nt':
        roots=[]
        if os.environ.get('VCToolsRedistDir'):roots.append(Path(os.environ['VCToolsRedistDir']))
        locator=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        if locator.is_file():
            result=subprocess.run([str(locator),'-all','-products','*','-property','installationPath'],capture_output=True,text=True,check=True)
            for installation in result.stdout.splitlines():
                base=Path(installation.strip())/'VC/Redist/MSVC'
                if base.is_dir():roots.extend(sorted(base.iterdir(),reverse=True))
        crt=next((folder for base in roots for folder in (base/architecture).glob('Microsoft.VC*.CRT') if (folder/'msvcp140.dll').is_file()),None)
        if crt is None:raise ValueError('Microsoft VC runtime redistributables are missing; repair the build tools installation')
        for file in RuntimeLibraries(crt,architecture):
            shutil.copy2(file,output/file.name)
            if file.name not in names:names.append(file.name)
    sdk=output/'ScriptSDK';shutil.copytree(build/'ScriptSDK/include',sdk/'include',dirs_exist_ok=True);(sdk/'lib').mkdir(parents=True,exist_ok=True)
    library=build/'ScriptSDK/lib/Release/Bazzalt.lib'
    if not library.exists():library=build/'ScriptSDK/lib/Bazzalt.lib'
    if linux:
        for name in names:
            if name!='BazzaltPlayer':shutil.copy2(native/name,sdk/'lib'/name)
    else:shutil.copy2(library,sdk/'lib/Bazzalt.lib')
    licenses=output/'licenses';licenses.mkdir(exist_ok=True)
    try:from .collect_third_party import Collect
    except ImportError:from collect_third_party import Collect
    Collect(licenses,root)
    for file in (build/'ScriptSDK/licenses').glob('*'):
        if file.is_file():shutil.copy2(file,licenses/file.name)
    for source,name in ((root/'LICENSE','Bazzalt.txt'),(root/'deps/lua/README','Lua.txt'),(root/'deps/sol2/LICENSE.txt','Sol2.txt'),(root/'deps/filament/LICENSE','Filament.txt'),(root/'deps/SDL3/LICENSE.txt','SDL3.txt')):
        if source.is_file():shutil.copy2(source,licenses/name)
    versions=json.loads((root/'releases/versions.json').read_text())
    (output/'runtime.json').write_text(json.dumps(dict(format_version=1,game_format_version=3,core_version=versions['core'],platform=platform,architecture=architecture,files=names),indent=2),encoding='utf-8')
    return output
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--platform',choices=('windows','linux','macos'));parser.add_argument('--build',type=Path,default=Path('build'));parser.add_argument('--output',type=Path,required=True);parser.add_argument('--architecture',choices=('x64','x86','arm64'),required=True);args=parser.parse_args();Stage(args.build,args.output,args.architecture,platform=args.platform)
