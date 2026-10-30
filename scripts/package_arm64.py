"""Package already-built ARM64 Core and game runtime artifacts."""
import argparse
import json
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from scripts.stage_game_runtime import Stage
from scripts.release_artifacts import Audit,Archive,Entry
from scripts.collect_third_party import Collect

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--platform',choices=('windows','linux'),required=True);args=parser.parse_args();platform=args.platform
    build=ROOT/'build/arm64';stage=build/'CorePackage';stage.mkdir(parents=True,exist_ok=False);shutil.copytree(build/'ScriptSDK/include',stage/'include');shutil.copytree(build/'ScriptSDK/lib',stage/'lib')
    Stage(build,stage/('RuntimeTemplates/'+platform+'-arm64'),'arm64',platform=platform)
    version=json.loads((ROOT/'releases/versions.json').read_text())['core'];(stage/'core.json').write_text(json.dumps(dict(version=version,architecture='arm64',platform=platform,abi=1,lua_version='5.4.9',gui_abi=6,physics_abi=2)))
    Collect(stage/'licenses',ROOT);Audit(stage,'arm64','core',platform);output=ROOT/'dist'/('Bazzalt-core-'+version+'-'+platform+'-arm64.zip');Archive(stage,output);output.with_suffix('.zip.json').write_text(json.dumps(Entry(output,'core',version,'arm64','',platform),indent=2))
if __name__=='__main__':main()
