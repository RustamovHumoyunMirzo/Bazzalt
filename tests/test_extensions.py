import copy,json,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
import yaml
from bazzalt.extensions import BuildPackage,ExtractPackage,ExtensionStore,ExtensionError,ReadManifest,Version,Validate
from Compiler.extensions import BuildExtensionPackage,RuntimeIndex,LockProject,LockedItems
ROOT=Path(__file__).resolve().parents[1]
class ExtensionTests(unittest.TestCase):
    def fixture(self,root,identity='sample',requires=(),module='Sample'):
        root=Path(root);root.mkdir(parents=True,exist_ok=True);(root/'sdk.lua').write_text('return {Double=function(value) return value*2 end}',encoding='utf-8')
        value={'General':{'Id':identity,'Name':identity,'ShortDescription':'A test extension','Version':'1.0.0'},'Compatibility':{'MinEngineVersion':'0.7.0'},'Dependencies':{'Requires':list(requires)},'AssetRegistry':[{'Id':'sdk','Path':'sdk.lua'}],'Scripts':{'sdk':{'Id':'sdk','Public':True,'ModuleName':module}}}
        path=root/'extension.yaml';path.write_text(yaml.safe_dump(value),encoding='utf-8');return path,value
    def store(self,root):return ExtensionStore(root,'0.7.0','Windows')
    def test_build_import_disabled_enable_dependency_and_collision(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');package=BuildPackage(source,root/'sample.bpack');store=self.store(root/'store');self.assertEqual(store.Import(package),'sample');self.assertFalse(store.Installed()[0]['Enabled']);store.SetEnabled('sample',True);self.assertEqual([v['Id'] for v in store.Resolve()],['sample'])
            source,value=self.fixture(root/'dependent','dependent',['sample'],'Other');store.Import(BuildPackage(source,root/'other.bpack'));store.SetEnabled('dependent',True)
            with self.assertRaises(ExtensionError):store.SetEnabled('sample',False)
            source,value=self.fixture(root/'collision','collision',module='Sample');store.Import(BuildPackage(source,root/'collision.bpack'))
            with self.assertRaises(ExtensionError):store.SetEnabled('collision',True)
            self.assertFalse(next(i for i in store.Installed() if i['Id']=='collision')['Enabled'])
    def test_paths_duplicates_integrity_and_compatibility(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');value['AssetRegistry'][0]['Path']='../outside.lua'
            with self.assertRaises(ExtensionError):Validate(value,source.parent)
            for name in ('../outside','/absolute','C:/absolute','a\\b','CON','a/../../escape'):
                path=root/'bad.bpack'
                with zipfile.ZipFile(path,'w') as archive:archive.writestr(name,b'bad')
                with self.assertRaises(ExtensionError):ExtractPackage(path,root/'unpack')
            package=BuildPackage(source,root/'sample.bpack')
            with zipfile.ZipFile(package,'a') as archive:archive.writestr('sdk.lua',b'changed')
            with self.assertRaises(ExtensionError):self.store(root/'store').Import(package)
            source,value=self.fixture(root/'incompatible');value['Compatibility']['MinEngineVersion']='9.0.0';source.write_text(yaml.safe_dump(value),encoding='utf-8')
            with self.assertRaises(ExtensionError):self.store(root/'store').Import(BuildPackage(source,root/'new.bpack'))
    def test_yaml_duplicate_keys_and_nonfinite_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source,value=self.fixture(Path(temp))
            source.write_text(source.read_text(encoding='utf-8')+'\nGeneral: {}',encoding='utf-8')
            with self.assertRaises(ExtensionError):ReadManifest(source)
            value['General']['N']=float('nan')
            with self.assertRaises(ExtensionError):Validate(value,Path(temp))
        self.assertLess(Version('1.0.0-beta.2'),Version('1.0.0-beta.10'));self.assertLess(Version('1.0.0-beta.10'),Version('1.0.0'))
    def test_cancel_preserves_previous_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');output=root/'sample.bpack';output.write_bytes(b'previous')
            with self.assertRaises(ExtensionError):BuildPackage(source,output,cancel=lambda:True)
            self.assertEqual(output.read_bytes(),b'previous')
    def test_actual_lua_bytecode_and_sdk_only_runtime_deployment(self):
        if not (ROOT/'build/Editor/Release/bazzalt_lua.dll').exists() and not (ROOT/'build/Editor/libbazzalt_lua.so').exists():self.skipTest('host Lua compiler library is not installed')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');(source.parent/'editor.lua').write_text('return {}',encoding='utf-8');value['AssetRegistry'].append({'Id':'editor','Path':'editor.lua'});value['Scripts']['editor']={'Id':'editor','Entry':True};source.write_text(yaml.safe_dump(value),encoding='utf-8')
            platform='windows' if __import__('os').name=='nt' else 'linux';output=BuildExtensionPackage(source,root/'sample.bpack',ROOT/'build/Editor/Release',platform,'x64');store=ExtensionStore(root/'store','0.7.0',platform.title());store.Import(output);store.SetEnabled('sample',True);items=store.Resolve();project=root/'project';project.mkdir();LockProject(project,items)
            with patch('Compiler.extensions.StoreRoot',return_value=store.Root):locked=LockedItems(project,platform,'x64')
            staged=RuntimeIndex(locked,root/'game/ext',platform,'x64');index=json.loads((staged/'runtime.json').read_text(encoding='utf-8'));self.assertEqual([m['Name'] for m in index['Modules']],['Sample']);module=staged/index['Modules'][0]['Path'];self.assertTrue(module.read_bytes().startswith(b'\x1bLua'));self.assertFalse(list(staged.rglob('*.lua')))
            self.assertFalse(any('editor' in str(p) for p in staged.rglob('*')))

    def test_runtime_assets_have_stable_ids_and_modified_files_are_not_overwritten(self):
        from Compiler.extensions import MaterializeAssets,AssetUUID
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');(source.parent/'data.txt').write_text('payload');value['AssetRegistry'].append({'Id':'data','Path':'data.txt'});source.write_text(yaml.safe_dump(value),encoding='utf-8');store=self.store(root/'store');store.Import(BuildPackage(source,root/'sample.bpack'));store.SetEnabled('sample',True);items=store.Resolve();project=root/'project';project.mkdir();MaterializeAssets(project,items)
            asset=project/'Assets/Extensions/sample/data.txt';meta=yaml.safe_load(asset.with_name(asset.name+'.meta').read_text());self.assertEqual(meta['UUID'],AssetUUID('sample','data'));self.assertEqual(asset.read_text(),'payload');asset.write_text('user edit')
            with self.assertRaises(ExtensionError):MaterializeAssets(project,items)
            self.assertEqual(asset.read_text(),'user edit')
    def test_missing_dependency_cycle_and_private_name_collisions(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);store=self.store(root/'store')
            for name,dependency in (('first','second'),('second','first')):
                source,value=self.fixture(root/name,name,[dependency],name.title());store.Import(BuildPackage(source,root/(name+'.bpack')))
            with self.assertRaises(ExtensionError):store.Resolve(['first','second'])
            with self.assertRaises(ExtensionError):store.SetEnabled('first',True)
            source,value=self.fixture(root/'bad');value['Scripts']['private']={'Id':'sdk','ModuleName':'Sample','SelfContained':False};value['Scripts']['sdk']['SelfContained']=False
            with self.assertRaises(ExtensionError):Validate(value,source.parent)
    def test_old_pins_survive_installing_a_new_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');store=self.store(root/'store');store.Import(BuildPackage(source,root/'old.bpack'));store.SetEnabled('sample',True);project=root/'project';project.mkdir();LockProject(project,store.Resolve());store.SetEnabled('sample',False);value['General']['Version']='2.0.0';source.write_text(yaml.safe_dump(value),encoding='utf-8');store.Import(BuildPackage(source,root/'new.bpack'))
            with patch('Compiler.extensions.StoreRoot',return_value=store.Root):items=LockedItems(project,'windows','x64')
            self.assertEqual(items[0]['Manifest']['General']['Version'],'1.0.0')

    def test_editor_native_payload_is_never_staged_as_game_code(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source,value=self.fixture(root/'source');value['Scripts']['sdk'].update(Public=False,Entry=True);folder=source.parent/'editorbin';folder.mkdir();(folder/'Qt6Widgets.dll').write_bytes(b'editor-only fixture')
            value['Native']={'Enabled':True,'BinDirsWin':['editorbin/']};source.write_text(yaml.safe_dump(value),encoding='utf-8');store=self.store(root/'store');store.Import(BuildPackage(source,root/'plugin.bpack'));store.SetEnabled('sample',True);items=store.Resolve();self.assertEqual(items[0]['Manifest']['Native']['Scope'],'Editor')
            staged=RuntimeIndex(items,root/'game/ext','windows','x64');self.assertEqual(list(staged.rglob('*.dll')),[]);self.assertEqual(json.loads((staged/'runtime.json').read_text())['Modules'],[])
