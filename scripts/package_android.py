"""Stage an already-built Android Core SDK and release catalog entry."""
import argparse
import json
from pathlib import Path
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.stage_game_runtime import Stage
from scripts.release_artifacts import Architecture, Archive, Entry


def Package(architecture):
    root=Path(__file__).resolve().parents[1]
    build=root/'build/android'
    stage=build/'AndroidCorePackage'
    if stage.exists():raise ValueError('Android Core staging directory already exists')
    template=Stage(build,stage/('RuntimeTemplates/android-'+architecture),architecture,root,platform='android')
    shutil.copytree(template/'ScriptSDK/include',stage/'include')
    shutil.copytree(template/'ScriptSDK/lib',stage/'lib')
    shutil.copytree(template/'licenses',stage/'licenses')
    version=json.loads((root/'releases/versions.json').read_text())['core']
    (stage/'core.json').write_text(json.dumps(dict(version=version,architecture=architecture,platform='android',
        abi=1,lua_version='5.4.9',gui_abi=6,physics_abi=2,android_ndk='29.0.14206865',minimum_api=26)),encoding='utf-8')
    for path in stage.rglob('*.so'):
        if Architecture(path)!=architecture:raise ValueError('Wrong Android library architecture: '+str(path))
    output=root/'dist/releases'
    output.mkdir(parents=True,exist_ok=True)
    archive=output/f'Bazzalt-core-{version}-android-{architecture}.zip'
    Archive(stage,archive)
    archive.with_suffix('.zip.json').write_text(json.dumps(Entry(archive,'core',version,architecture,'','android'),indent=2),encoding='utf-8')
    return archive


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--architecture',choices=('arm64','x64'),required=True)
    Package(parser.parse_args().architecture)
