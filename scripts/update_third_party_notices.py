"""Refresh license-only snapshots from the pinned Qt and Filament source tags.

This downloads notices, not dependency sources or binaries. Release builds use
the checked-in snapshot and never need this network step.
"""
import hashlib
import json
from pathlib import Path,PurePosixPath
import urllib.request

ROOT=Path(__file__).resolve().parents[1]/'licenses/third_party'
FILAMENT='v1.77.0'
QT='v6.10.2'

def Read(url):
    request=urllib.request.Request(url,headers={'User-Agent':'Bazzalt-license-inventory','Accept':'application/vnd.github+json'})
    with urllib.request.urlopen(request,timeout=30) as response:
        data=response.read(1024*1024+1)
    if len(data)>1024*1024:raise ValueError('Notice exceeds size limit: '+url)
    return data

def Update():
    ROOT.mkdir(parents=True,exist_ok=True);records=[]
    def save(relative,url):
        path=PurePosixPath(relative)
        if path.is_absolute() or any(part in ('..','.') for part in path.parts):raise ValueError('Unsafe notice path')
        data=Read(url);target=ROOT.joinpath(*path.parts)
        if not target.resolve().is_relative_to(ROOT.resolve()):raise ValueError('Notice path escapes snapshot directory')
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        records.append(dict(file=path.as_posix(),source=url,sha256=hashlib.sha256(data).hexdigest()))
    for name in ('LGPL-3.0-only.txt','GPL-3.0-only.txt'):
        save('Qt/'+name,f'https://raw.githubusercontent.com/qt/qtbase/{QT}/LICENSES/{name}')
    # Only tree metadata is requested; the repository source is not downloaded.
    request=urllib.request.Request(f'https://api.github.com/repos/google/filament/git/trees/{FILAMENT}',headers={'User-Agent':'Bazzalt-license-inventory'})
    with urllib.request.urlopen(request,timeout=60) as response:top=json.loads(response.read(1024*1024))
    third_party=next(item['sha'] for item in top['tree'] if item['path']=='third_party')
    request=urllib.request.Request(f'https://api.github.com/repos/google/filament/git/trees/{third_party}?recursive=1',headers={'User-Agent':'Bazzalt-license-inventory'})
    with urllib.request.urlopen(request,timeout=60) as response:
        tree=json.loads(response.read(16*1024*1024))
    if tree.get('truncated'):raise ValueError('Incomplete upstream notice inventory')
    for item in tree['tree']:
        path=PurePosixPath(item['path']);name=path.name.lower()
        if item['type']!='blob':continue
        if not (name.startswith(('license','copying','notice','copyright')) or name=='mikktspace.h'):continue
        if path.suffix.lower() not in ('','.txt','.md','.rst','.rtf','.h','.xml','.xsl'):continue
        if item.get('size',0)>1024*1024:raise ValueError('Unexpected notice size')
        save('Filament/third_party/'+item['path'],f'https://raw.githubusercontent.com/google/filament/{FILAMENT}/third_party/{item["path"]}')
    (ROOT/'sources.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    print(f'Refreshed {len(records)} upstream notice files')

if __name__=='__main__':Update()
