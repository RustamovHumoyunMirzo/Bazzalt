"""Stage macOS products from an existing build; never build large dependencies."""
import argparse,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from scripts.release_artifacts import Audit,Archive,Entry
from scripts.stage_game_runtime import Stage
from bazzalt.platforms import HostArchitecture
from Compiler.macos import Sign

def Run(*args):subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
def Libraries(build,destination):
    destination.mkdir(parents=True,exist_ok=True)
    for pattern in ('libBazzalt*.dylib','libbazzalt_lua*.dylib','libbazzalt_gui*.dylib','libbazzalt_physics*.dylib','libbshad*.dylib'):
        for path in (build/'Editor').glob(pattern):shutil.copy2(path,destination/path.name)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--product',choices=('core','editor','hub','compiler','all'),default='all');parser.add_argument('--build',type=Path,default=ROOT/'build');parser.add_argument('--output',type=Path,default=ROOT/'dist/releases');parser.add_argument('--base-url',default='');args=parser.parse_args()
    if sys.platform!='darwin':raise SystemExit('macOS release packaging requires macOS')
    architecture=HostArchitecture()
    profile={'signing_identity':os.environ.get('BAZZALT_MAC_SIGNING_IDENTITY','-')}
    versions=json.loads((ROOT/'releases/versions.json').read_text());build=args.build.resolve();args.output.mkdir(parents=True,exist_ok=True)
    products=('core','compiler','editor','hub') if args.product=='all' else (args.product,)
    from scripts.collect_third_party import Collect
    for product in products:
        stage=build/'macOSRelease'/product
        if stage.exists():raise SystemExit('Choose a clean staging directory: '+str(stage))
        if product=='core':
            stage.mkdir(parents=True);shutil.copytree(build/'ScriptSDK/include',stage/'include');Libraries(build,stage/'lib');Stage(build,stage/('RuntimeTemplates/macos-'+architecture),architecture,platform='macos')
            (stage/'core.json').write_text(json.dumps(dict(version=versions['core'],architecture=architecture,platform='macos',abi=1,lua_version='5.4.9',gui_abi=6,physics_abi=2)))
        else:
            executable={'editor':'Bazzalt','hub':'BazzaltHub','compiler':'BazzaltCompiler'}[product];source={'editor':'bazzalt_editor.py','hub':'bazzalt_hub.py','compiler':'bazzalt_compiler.py'}[product]
            command=[sys.executable,'-m','nuitka','--mode=standalone','--assume-yes-for-downloads','--output-dir='+str(build/'macOSNuitka'/product),'--output-filename='+executable]
            if product=='compiler':command+=['--nofollow-import-to=PySide6,Editor.gui,Editor.runtime,Compiler.tests','--include-module=Editor.scripting','--include-module=Editor.property_rules','--include-module=Editor.lua_declarations','--include-module=scripts.release_artifacts','--include-module=Compiler.macos','--include-data-files='+str(ROOT/'releases/versions.json')+'=releases/versions.json']
            else:
                Run(sys.executable,'scripts/compile_resources.py','--build-directory',build/'CompiledResources')
                if product=='editor':command+=['--include-module=PySide6.QtUiTools','--include-package=keyring.backends','--include-distribution-metadata=keyring']
                command+=['--enable-plugin=pyside6','--include-module=bazzalt._branding_resources_rc','--include-module=bazzalt._'+('hub' if product=='hub' else 'editor')+'_resources_rc','--nofollow-import-to=PySide6.QtWebEngineCore,PySide6.QtWebEngineWidgets,PySide6.QtWebChannel']
            if product!='compiler':command+=['--macos-create-app-bundle','--macos-app-name='+executable]
            Run(*command,ROOT/source)
            if product=='compiler':shutil.copytree(build/'macOSNuitka'/product/(Path(source).stem+'.dist'),stage);program=stage
            else:
                stage.mkdir(parents=True);candidates=list((build/'macOSNuitka'/product).glob('*.app'))
                if len(candidates)!=1:raise ValueError('Expected one Nuitka app bundle')
                shutil.copytree(candidates[0],stage/(executable+'.app'),symlinks=True);program=stage/(executable+'.app/Contents/MacOS')
            if product=='compiler':
                Libraries(build,program);shutil.copy2(build/'Editor/BazzaltCooker',stage/'BazzaltCooker');(stage/'filament').mkdir()
                for tool in ('matc','cmgen','filamesh'):shutil.copy2(ROOT/'deps/filament/bin'/tool,stage/'filament'/tool)
            elif product=='editor':
                Libraries(build,program/'Editor');modules=list((build/'Editor').glob('_bazzalt_runtime*.so'))
                if len(modules)!=1:raise ValueError('Expected one native editor module')
                shutil.copy2(modules[0],program/'Editor'/modules[0].name);shutil.copytree(build/'ScriptSDK',program/'ScriptSDK',symlinks=False);Stage(build,program/('RuntimeTemplates/macos-'+architecture),architecture,platform='macos')
                compiler=build/'macOSRelease/compiler'
                if not compiler.is_dir():raise ValueError('Package the macOS compiler before the editor')
                shutil.copytree(compiler,program/'tools/compiler');(stage/'editor.json').write_text(json.dumps(dict(version=versions['editor'],architecture=architecture,platform='macos',executable='Bazzalt.app/Contents/MacOS/Bazzalt',project_format_max=1,lua_version='5.4.9',gui_abi=6,physics_abi=2)))
        Collect(stage/'licenses',ROOT,python=product!='core')
        if product=="hub":
            (stage/"share").mkdir(exist_ok=True);shutil.copy2(ROOT/"bazzalt/assets/branding/BazzaltIcon.svg",stage/"share/BazzaltLogo.svg")

        
        for path in sorted(stage.rglob('*.dylib')):Sign(path,profile)
        if product in ('editor','hub'):Run('/usr/bin/codesign','--force','--deep','--sign',profile['signing_identity'],stage/(executable+'.app'));Run('/usr/bin/codesign','--verify','--deep','--strict',stage/(executable+'.app'))
        Audit(stage,architecture,product,'macos')
        version=versions.get(product,versions['core']);output=args.output/f'Bazzalt-{product}-{version}-macos-{architecture}.zip';Archive(stage,output,bundle_links=True);output.with_suffix('.zip.json').write_text(json.dumps(Entry(output,product,version,architecture,args.base_url,'macos'),indent=2))
if __name__=='__main__':main()
