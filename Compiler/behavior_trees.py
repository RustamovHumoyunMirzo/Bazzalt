"""Validate and lower .bbtree sources for every game target."""
from pathlib import Path
import yaml
from Editor import behavior_tree as Behavior

def CompileBehaviorTrees(cooked,check=lambda:None):
    cooked=Path(cooked).resolve()
    records=yaml.safe_load((cooked/'.bazzalt/CookedAssets.yaml').read_text(encoding='utf-8')).get('Assets') or []
    for record in records:
        if Path(record['Source']).suffix.lower()!='.bbtree':continue
        check()
        source=(cooked/record['Source']).resolve();cache=(cooked/record['Cache']).resolve()
        if not source.is_relative_to(cooked) or not cache.is_relative_to(cooked):raise ValueError('Behavior tree path escapes cooked content')
        if record.get('Importer')!='Bazzalt.BehaviorTree':raise ValueError('Behavior tree has an incompatible importer: '+record['Source'])
        root=Behavior.Load(source)
        root.attrib.clear();root.attrib.update(version='1',locked='false')
        for node in root.iter():
            metadata=['id','editor-x','editor-y','collapsed','comment','comment-open']
            # Legacy layout coordinates only occur on nodes without vector fields.
            if node.tag in Behavior.NODES and 'x' not in Behavior.NODES[node.tag][1]:metadata+=['x','y']
            for name in metadata:node.attrib.pop(name,None)
        cache.parent.mkdir(parents=True,exist_ok=True)
        Behavior.Save(cache,Behavior.Source(root))
