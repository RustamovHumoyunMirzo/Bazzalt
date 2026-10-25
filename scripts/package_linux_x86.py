"""Package a prebuilt Linux x86 Core and runtime template; never build dependencies."""
import json,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from scripts.stage_game_runtime import Stage
from scripts.release_artifacts import Audit,Archive,Entry

def main():
    build=ROOT/'build/linux-x86';output=ROOT/'dist/linux-x86';stage=build/'CorePackage'
    if stage.exists():raise ValueError('Core staging directory already exists')
    stage.mkdir(parents=True);output.mkdir(parents=True,exist_ok=True)
    shutil.copytree(build/'ScriptSDK/include',stage/'include');(stage/'lib').mkdir()
    for pattern in ('libBazzalt.so*','libbazzalt_lua.so*','libbazzalt_gui.so*'):
        for path in (build/'Editor').glob(pattern):shutil.copy2(path,stage/'lib'/path.name)
    Stage(build,stage/'RuntimeTemplates/linux-x86','x86',platform='linux')
    version=json.loads((ROOT/'releases/versions.json').read_text())['core']
    (stage/'core.json').write_text(json.dumps(dict(version=version,architecture='x86',platform='linux',abi=1,lua_version='5.4.9',gui_abi=6)))
    Audit(stage,'x86','core','linux');archive=output/f'Bazzalt-core-{version}-linux-x86.zip';Archive(stage,archive)
    archive.with_suffix('.zip.json').write_text(json.dumps(Entry(archive,'core',version,'x86','','linux'),indent=2))
if __name__=='__main__':main()
