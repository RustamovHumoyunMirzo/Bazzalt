"""Opt-in output inclusion rules, with serialized asset dependencies retained.

Rules do not guess assets dynamically loaded by game code. Keep those explicitly.
This operates only on the disposable cooked tree, never the source project.
"""
import fnmatch
from pathlib import Path
import yaml
from .pak import SafeName


def FilterCooked(cooked, packaging):
    include=[p.strip() for p in packaging['include_assets'].splitlines() if p.strip()]
    exclude=[p.strip() for p in packaging['exclude_assets'].splitlines() if p.strip()]
    cooked=Path(cooked).resolve();index=cooked/'.bazzalt/CookedAssets.yaml'
    data=yaml.safe_load(index.read_text(encoding='utf-8'));records=data.get('Assets') or []
    for row in records:
        row['Source']=row['Source'].replace('\\','/');row['Cache']=row['Cache'].replace('\\','/')
    # Compilation-state fingerprints and translated intermediate source are
    # not runtime products. Reflection and the binary material package are.
    shaders={'.mat','.shad','.bshader'}
    for row in records:
        if Path(row['Source']).suffix.lower() not in shaders:continue
        for suffix in ('.build.json','.translated.mat'):
            path=cooked/(row['Cache']+suffix)
            if not path.resolve().is_relative_to(cooked):raise ValueError('Shader product escapes its workspace')
            if path.is_file():path.unlink()
    if include==['**'] and not exclude:return
    by_id={str(row['UUID']).casefold():row for row in records};by_path={row['Source'].casefold():str(row['UUID']).casefold() for row in records}
    def Matches(path,patterns):return any(fnmatch.fnmatchcase(path.casefold(),pattern.casefold()) for pattern in patterns)
    def Local(name):
        SafeName(name);path=cooked/name
        if not path.resolve().is_relative_to(cooked):raise ValueError('Cooked content escapes its workspace')
        return path
    denied={identity for identity,row in by_id.items() if Matches(row['Source'],exclude)}
    kept={identity for identity,row in by_id.items() if identity not in denied and Matches(row['Source'],include)}
    project=yaml.safe_load((cooked/'Game.bproject').read_text(encoding='utf-8'))
    startup=by_path.get(str(project['StartupScene']).replace('\\','/').casefold())
    if startup is None or startup in denied:raise ValueError('Startup scene is excluded from the game build')
    kept.add(startup);queue=list(kept);visited=set()
    application=str((project.get('Properties') or {}).get('application.script','')).casefold()
    if application:
        if application not in by_id or application in denied:raise ValueError('Application script is missing or excluded from the game build')
        kept.add(application);queue.append(application)
    serialized={'.bscene','.matinst','.bmixer','.btexture'}
    def References(value):
        visited=set();steps=0
        def Visit(item,depth=0):
            nonlocal steps
            steps+=1
            if steps>1000000 or depth>128:raise ValueError('Serialized dependency graph exceeds limits')
            if isinstance(item,str):
                if item.casefold() in by_id:yield item.casefold()
                elif item.replace('\\','/').casefold() in by_path:yield by_path[item.replace('\\','/').casefold()]
            elif isinstance(item,(dict,list)):
                if id(item) in visited:return
                visited.add(id(item))
                for child in (item.values() if isinstance(item,dict) else item):yield from Visit(child,depth+1)
        yield from Visit(value)
    while queue:
        identity=queue.pop()
        if identity in visited:continue
        visited.add(identity);row=by_id[identity];source=Local(row['Source'])
        dependency_file=source if source.suffix.lower() in serialized else Path(str(Local(row['Cache']))+'.reflection.json') if source.suffix.lower() in shaders else None
        if dependency_file is None or not dependency_file.is_file():continue
        if dependency_file.stat().st_size>64*1024*1024:raise ValueError('Serialized dependency asset exceeds size limit')
        for dependency in References(yaml.safe_load(dependency_file.read_text(encoding='utf-8'))):
            if dependency in denied:raise ValueError('Excluded asset is required by '+row['Source']+': '+by_id[dependency]['Source'])
            if dependency not in kept:kept.add(dependency);queue.append(dependency)
    allowed={cooked/'Game.bproject',index}
    for identity in kept:
        row=by_id[identity];source=Local(row['Source']);cache=Local(row['Cache'])
        allowed.update((source,Path(str(source)+'.meta'),cache))
        if source.suffix.lower() in shaders:allowed.update((Path(str(cache)+'.filamat'),Path(str(cache)+'.reflection.json')))
        if row.get('Importer')=='Bazzalt.Environment':
            allowed.update(p for p in Path(str(cache)+'.environment').rglob('*') if p.is_file())
        if row.get('Importer')=='Bazzalt.glTF':
            allowed.update(p for p in cache.parent.rglob('*') if p.is_file())
    for path in cooked.rglob('*'):
        if path.is_symlink() or path.is_junction():raise ValueError('Linked cooked content is forbidden')
        if path.is_file() and path not in allowed:path.unlink()
    data['Assets']=[row for row in records if str(row['UUID']).casefold() in kept]
    index.write_text(yaml.safe_dump(data,allow_unicode=True,sort_keys=False),encoding='utf-8')
