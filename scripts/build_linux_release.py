"""Stage Linux products from an existing build; never build large dependencies."""
import argparse,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from scripts.release_artifacts import Audit,Archive,Entry
from scripts.stage_game_runtime import Stage

def Run(*args):subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
def Libraries(build,destination):
    destination.mkdir(parents=True,exist_ok=True)
    for pattern in ('libBazzalt.so*','libbazzalt_lua.so*','libbazzalt_gui.so*','libbshad.so*'):
        for path in (build/'Editor').glob(pattern):shutil.copy2(path,destination/path.name)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--product',choices=('core','editor','hub','compiler','all'),default='all');parser.add_argument('--build',type=Path,default=ROOT/'build');parser.add_argument('--output',type=Path,default=ROOT/'dist/releases');parser.add_argument('--base-url',default='');args=parser.parse_args()
    if not sys.platform.startswith('linux'):raise SystemExit('Linux release packaging requires Linux')
    versions=json.loads((ROOT/'releases/versions.json').read_text());build=args.build.resolve();args.output.mkdir(parents=True,exist_ok=True)
    products=('core','compiler','editor','hub') if args.product=='all' else (args.product,)
    from scripts.collect_third_party import Collect
    for product in products:
        stage=build/'LinuxRelease'/product
        if stage.exists():raise SystemExit('Choose a clean staging directory: '+str(stage))
        if product=='core':
            stage.mkdir(parents=True);shutil.copytree(build/'ScriptSDK/include',stage/'include');Libraries(build,stage/'lib');Stage(build,stage/'RuntimeTemplates/linux-x64','x64',platform='linux')
            (stage/'core.json').write_text(json.dumps(dict(version=versions['core'],architecture='x64',platform='linux',abi=1,lua_version='5.4.9',gui_abi=6)))
        else:
            executable={'editor':'Bazzalt','hub':'BazzaltHub','compiler':'BazzaltCompiler'}[product];source={'editor':'bazzalt_editor.py','hub':'bazzalt_hub.py','compiler':'bazzalt_compiler.py'}[product]
            command=[sys.executable,'-m','nuitka','--mode=standalone','--assume-yes-for-downloads','--output-dir='+str(build/'LinuxNuitka'/product),'--output-filename='+executable]
            if product=='compiler':command+=['--nofollow-import-to=PySide6,Editor.gui,Editor.runtime,Compiler.tests','--include-module=Editor.scripting','--include-module=Editor.property_rules','--include-module=Editor.lua_declarations','--include-module=scripts.release_artifacts','--include-data-files='+str(ROOT/'releases/versions.json')+'=releases/versions.json']
            else:
                Run(sys.executable,'scripts/compile_resources.py','--build-directory',build/'CompiledResources')
                command+=['--enable-plugin=pyside6','--include-module=bazzalt._branding_resources_rc','--include-module=bazzalt._'+('hub' if product=='hub' else 'editor')+'_resources_rc','--nofollow-import-to=PySide6.QtWebEngineCore,PySide6.QtWebEngineWidgets,PySide6.QtWebChannel']
            Run(*command,ROOT/source);shutil.copytree(build/'LinuxNuitka'/product/(Path(source).stem+'.dist'),stage)
            if product=='compiler':
                Libraries(build,stage);shutil.copy2(build/'Editor/BazzaltCooker',stage/'BazzaltCooker');(stage/'filament').mkdir()
                for tool in ('matc','cmgen','filamesh'):shutil.copy2(ROOT/'deps/filament/bin'/tool,stage/'filament'/tool)
            elif product=='editor':
                Libraries(build,stage/'Editor');modules=list((build/'Editor').glob('_bazzalt_runtime*.so'))
                if len(modules)!=1:raise ValueError('Expected one native editor module')
                shutil.copy2(modules[0],stage/'Editor'/modules[0].name);shutil.copytree(build/'ScriptSDK',stage/'ScriptSDK',symlinks=False);Stage(build,stage/'RuntimeTemplates/linux-x64','x64',platform='linux')
                compiler=build/'LinuxRelease/compiler'
                if not compiler.is_dir():raise ValueError('Package the Linux compiler before the editor')
                shutil.copytree(compiler,stage/'tools/compiler');(stage/'editor.json').write_text(json.dumps(dict(version=versions['editor'],architecture='x64',platform='linux',executable='Bazzalt',project_format_max=1,lua_version='5.4.9',gui_abi=6)))
        Collect(stage/'licenses',ROOT,python=product!='core')
        x86=ROOT/'build/LinuxX86Package/RuntimeTemplates/linux-x86'
        if product in ('core','editor') and x86.is_dir():shutil.copytree(x86,stage/'RuntimeTemplates/linux-x86')
        if product=="hub":
            (stage/"share").mkdir(exist_ok=True);shutil.copy2(ROOT/"Launcher/BazzaltLogo.svg",stage/"share/BazzaltLogo.svg")
            shutil.copy2(ROOT/'scripts/install_linux.sh',stage/'install_linux.sh')
        Audit(stage,'x64',product,'linux')
        version=versions.get(product,versions['core']);output=args.output/f'Bazzalt-{product}-{version}-linux-x64.zip';Archive(stage,output);output.with_suffix('.zip.json').write_text(json.dumps(Entry(output,product,version,'x64',args.base_url,'linux'),indent=2))
if __name__=='__main__':main()
