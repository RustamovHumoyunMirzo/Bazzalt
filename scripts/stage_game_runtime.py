"""Assemble a prebuilt runtime template. Does not compile the engine."""
import argparse,json,shutil,os,subprocess
from pathlib import Path
try:from .release_artifacts import Architecture
except ImportError:from release_artifacts import Architecture

def Stage(build,output,architecture,root=None):
    build=Path(build).resolve();output=Path(output).resolve();root=Path(root or Path(__file__).resolve().parents[1]);native=build/'Editor/Release'
    output.mkdir(parents=True,exist_ok=True)
    names=['BazzaltPlayer.exe','Bazzalt.dll','bazzalt_lua.dll','bazzalt_gui.dll']
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
            result=subprocess.run([str(locator),'-all','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],capture_output=True,text=True,check=True)
            for installation in result.stdout.splitlines():
                base=Path(installation.strip())/'VC/Redist/MSVC'
                if base.is_dir():roots.extend(sorted(base.iterdir(),reverse=True))
        crt=next((folder for base in roots for folder in (base/architecture).glob('Microsoft.VC*.CRT') if (folder/'msvcp140.dll').is_file()),None)
        if crt is None:raise ValueError('Microsoft VC runtime redistributables are missing; repair the build tools installation')
        for file in crt.glob('*.dll'):
            if Architecture(file)!=architecture:raise ValueError('Wrong VC runtime architecture')
            shutil.copy2(file,output/file.name)
            if file.name not in names:names.append(file.name)
    sdk=output/'ScriptSDK';shutil.copytree(build/'ScriptSDK/include',sdk/'include',dirs_exist_ok=True);(sdk/'lib').mkdir(parents=True,exist_ok=True)
    library=build/'ScriptSDK/lib/Release/Bazzalt.lib'
    if not library.exists():library=build/'ScriptSDK/lib/Bazzalt.lib'
    shutil.copy2(library,sdk/'lib/Bazzalt.lib')
    licenses=output/'licenses';licenses.mkdir(exist_ok=True)
    for file in (build/'ScriptSDK/licenses').glob('*'):
        if file.is_file():shutil.copy2(file,licenses/file.name)
    for source,name in ((root/'LICENSE','Bazzalt.txt'),(root/'deps/lua/README','Lua.txt'),(root/'deps/sol2/LICENSE.txt','Sol2.txt'),(root/'deps/filament/LICENSE','Filament.txt'),(root/'deps/SDL3/LICENSE.txt','SDL3.txt')):
        if source.is_file():shutil.copy2(source,licenses/name)
    versions=json.loads((root/'releases/versions.json').read_text())
    (output/'runtime.json').write_text(json.dumps(dict(format_version=1,game_format_version=2,core_version=versions['core'],architecture=architecture,files=names),indent=2),encoding='utf-8')
    return output
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,default=Path('build'));parser.add_argument('--output',type=Path,required=True);parser.add_argument('--architecture',choices=('x64','x86'),required=True);args=parser.parse_args();Stage(args.build,args.output,args.architecture)
