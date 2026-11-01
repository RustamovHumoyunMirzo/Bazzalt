"""Fetch the exact, unmodified Yojimbo source and bundled dependencies."""
import json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
lock=json.loads((ROOT/'scripts/dependencies/yojimbo.json').read_text())
target=ROOT/'deps/yojimbo'
def git(*args):
    return subprocess.check_output(['git',*args],text=True).strip()
if target.exists():
    if not (target/'.git').exists() or git('-C',str(target),'status','--porcelain'):
        raise SystemExit('Yojimbo source is not a clean Git checkout; preserve your changes before fetching.')
    if git('-C',str(target),'rev-parse','HEAD')!=lock['commit']:
        subprocess.run(['git','-C',str(target),'fetch','--depth','1','origin',lock['commit']],check=True)
else:
    target.parent.mkdir(exist_ok=True)
    subprocess.run(['git','clone','--no-checkout','--depth','1',lock['repository'],str(target)],check=True)
    subprocess.run(['git','-C',str(target),'fetch','--depth','1','origin',lock['commit']],check=True)
subprocess.run(['git','-C',str(target),'checkout','--detach',lock['commit']],check=True)
if git('-C',str(target),'rev-parse','HEAD')!=lock['commit']:raise SystemExit('Yojimbo revision mismatch')
print('Installed pinned Yojimbo and bundled netcode/reliable/serialize/libsodium/tlsf sources')
