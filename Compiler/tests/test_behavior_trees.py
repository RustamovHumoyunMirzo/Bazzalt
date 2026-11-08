import copy,tempfile,unittest
from pathlib import Path
import yaml
from Compiler.behavior_trees import CompileBehaviorTrees
from Compiler.content import FilterCooked
from Compiler.settings import DEFAULTS
from Editor.behavior_tree import Load

class BehaviorCompilerTests(unittest.TestCase):
    def Fixture(self,root):
        records=[]
        for identity,name,source in [('scene','Main.bscene','Tree: npc\n'),('npc','NPC.bbtree','<BehaviorTree version="1" locked="true"><Blackboard><String key="asset" value="model"/></Blackboard><Tree><Succeed id="block" x="4" comment="hello"/></Tree></BehaviorTree>'),('model','Model.bin','model'),('unused','Unused.bin','unused')]:
            path=root/'Assets'/name;cache=root/'.bazzalt/Cache'/identity/('current'+path.suffix)
            path.parent.mkdir(parents=True,exist_ok=True);cache.parent.mkdir(parents=True,exist_ok=True);path.write_text(source);cache.write_text(source)
            records.append(dict(UUID=identity,Source=path.relative_to(root).as_posix(),Cache=cache.relative_to(root).as_posix(),Importer='Bazzalt.BehaviorTree' if identity=='npc' else 'Bazzalt.Raw'))
        (root/'Game.bproject').write_text('StartupScene: Assets/Main.bscene\n')
        (root/'.bazzalt/CookedAssets.yaml').write_text(yaml.safe_dump(dict(Assets=records)))
        return records
    def test_compile_and_package_transitive_references(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);records=self.Fixture(root);CompileBehaviorTrees(root)
            runtime=Load(root/records[1]['Cache']);self.assertEqual(runtime.get('locked'),'false');self.assertEqual(runtime.find('Tree')[0].attrib,{})
            self.assertIn('comment=',(root/'Assets/NPC.bbtree').read_text())
            settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene'
            FilterCooked(root,settings)
            self.assertTrue((root/records[1]['Cache']).is_file());self.assertTrue((root/'Assets/Model.bin').is_file());self.assertFalse((root/'Assets/Unused.bin').exists())
    def test_invalid_xml_and_excluded_dependencies_fail_build(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);(root/'Assets/NPC.bbtree').write_text('<BehaviorTree/>')
            with self.assertRaises(ValueError):CompileBehaviorTrees(root)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene';settings['exclude_assets']='Assets/Model.bin'
            with self.assertRaisesRegex(ValueError,'Excluded asset'):FilterCooked(root,settings)
    def test_compiler_rejects_escaping_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);records=self.Fixture(root);records[1]['Cache']='../escape.bbtree';(root/'.bazzalt/CookedAssets.yaml').write_text(yaml.safe_dump(dict(Assets=records)))
            with self.assertRaisesRegex(ValueError,'escapes'):CompileBehaviorTrees(root)
    def test_lowering_preserves_runtime_vectors(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);records=self.Fixture(root)
            (root/'Assets/NPC.bbtree').write_text('<BehaviorTree version="1"><Blackboard><Vector key="target" x="7" y="8" z="9"/></Blackboard><Tree><SetVelocity x="1" y="2" z="3" editor-x="80" editor-y="90"/></Tree></BehaviorTree>')
            CompileBehaviorTrees(root);runtime=Load(root/records[1]['Cache'])
            self.assertEqual(runtime.find('Tree')[0].attrib,dict(x='1',y='2',z='3'))
            self.assertEqual(runtime.find('Blackboard')[0].get('x'),'7')
