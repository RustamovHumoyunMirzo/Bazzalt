"""A protected standalone game must use its SDK without the editor store."""
import copy,json,os,subprocess,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import yaml
from bazzalt.extensions import ExtensionStore
from Compiler.extensions import BuildExtensionPackage,LockProject
from Compiler.build import Build
from Compiler.settings import DEFAULTS,ApplyPreset,Save
from scripts.stage_game_runtime import Stage
ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'build/Editor/Release'
@unittest.skipUnless(os.name=='nt' and (NATIVE/'BazzaltPlayer.exe').is_file() and (NATIVE/'BazzaltCooker.exe').is_file(),'Existing Windows native player/cooker required')
class ExtensionGameBuildTests(unittest.TestCase):
    def test_sdk_is_protected_and_editor_controller_is_not_shipped(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'Документы';root.mkdir();runtime=Stage(ROOT/'build',root/'Runtime','x64');author=root/'author';author.mkdir()
            (author/'math.lua').write_text('return {Double=function(v) return v*2 end}',encoding='utf-8');(author/'editor.lua').write_text('return {OnEnable=function() error("Editor code must never run in a game") end}',encoding='utf-8')
            manifest={'General':{'Id':'studio.math','Name':'Test SDK','ShortDescription':'Standalone game probe','Version':'1.0.0'},'Compatibility':{'MinEngineVersion':'0.7.0'},'AssetRegistry':[{'Id':'math','Path':'math.lua'},{'Id':'editor','Path':'editor.lua'}],'Scripts':{'math':{'Id':'math','Public':True,'ModuleName':'FixtureMath'},'editor':{'Id':'editor','Entry':True}}}
            (author/'helper.lua').write_text('return {Double=function(v) return v*2 end}',encoding='utf-8')
            (author/'owned.lua').write_text('local P=COMPONENT("OwnedProbe"); function P:OnCreate() assert(Bazzalt.require("studio.math.Helper").Double(5)==10) end; return P',encoding='utf-8')
            manifest['AssetRegistry'] += [{'Id':'helper','Path':'helper.lua'},{'Id':'owned','Path':'owned.lua'}]
            manifest['Scripts']['helper']={'Id':'helper','ModuleName':'Helper','Scope':'Runtime'}
            manifest['Scripts']['owned']={'Id':'owned','Scope':'Runtime'}
            manifest['Extends']={'Component':{'Owned':{'Script':'owned'}}}
            path=author/'extension.yaml';path.write_text(yaml.safe_dump(manifest),encoding='utf-8');package=BuildExtensionPackage(path,root/'math.bpack',runtime,'windows','x64');store=ExtensionStore(root/'Store','0.7.0','Windows');store.Import(package);store.SetEnabled('studio.math',True)
            assets=root/'Assets';assets.mkdir();project=root/'Game.bproject';project.write_text('FormatVersion: 1\nProjectUUID: "23456789-1234-4234-8234-123456789abc"\nName: Extension Test\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\nProperties: {}\n',encoding='utf-8');LockProject(project,store.Resolve())
            (assets/'Probe.lua').write_text('local P=COMPONENT("ExtensionProbe")\nfunction P:OnCreate() local M=Bazzalt.require("FixtureMath");assert(M.Double(4)==8);assert(Bazzalt.Extensions.Call("FixtureMath","Double",{3})==6);assert(not Bazzalt.Extensions.HasModule("studio.math.editor"));assert(not pcall(function() Bazzalt.require("studio.math.Helper") end)); end\nreturn P',encoding='utf-8')
            component={'Type':'Bazzalt.ScriptComponents','Version':1,'Properties':{'Count':'1','Enabled':'true','Script.0.Source':'Assets/Probe.lua','Script.0.TypeName':'ExtensionProbe','Script.0.Enabled':'true','Script.0.Lua':'true','Script.0.Count':'0'}}
            owned=copy.deepcopy(component);owned['Properties'].update({'Script.0.Source':'Assets/Extensions/studio.math/owned.lua','Script.0.TypeName':'OwnedProbe'})
            scene={'FormatVersion':1,'SceneUUID':'12345678-1234-4234-8234-123456789abc','Entities':[{'UUID':'34567890-1234-4234-8234-123456789abc','Parent':'00000000-0000-0000-0000-000000000000','Name':'Probe','Components':[component]}]};scene['Entities'].append({'UUID':'44567890-1234-4234-8234-123456789abc','Parent':'00000000-0000-0000-0000-000000000000','Name':'Owned','Components':[owned]});(assets/'Main.bscene').write_text(yaml.safe_dump(scene,sort_keys=False),encoding='utf-8')
            for format in ('loose','pak'):
                settings=ApplyPreset(DEFAULTS,'low' if format=='loose' else 'medium');settings['packaging']['format']=format;settings['intro']['enabled']=False;Save(project,settings)
                with patch('Compiler.extensions.StoreRoot',return_value=store.Root):output=Build(project,runtime,NATIVE/'BazzaltCooker.exe')
                if format=='loose':
                    index=json.loads((output/'Data/ext/runtime.json').read_text());self.assertCountEqual([m['Name'] for m in index['Modules']],['FixtureMath','studio.math.Helper']);self.assertEqual(index['Components'],{'OwnedProbe':'studio.math'});self.assertFalse(any(p.suffix in {'.lua','.cpp','.py','.ui'} for p in (output/'Data').rglob('*')))
                else:
                    self.assertTrue(list(output.glob('*.bpak')));self.assertFalse((output/'Data').exists())
                # The output must be independent of per-user installed packages.
                hidden=root/'UnavailableStore';store.Root.rename(hidden)
                try:
                    if os.environ.get('BAZZALT_GAME_GPU_TEST')=='1':
                        result=subprocess.run([str(output/'Game.exe'),'--smoke-test'],capture_output=True,timeout=60);self.assertEqual(result.returncode,0,result.stderr.decode('utf-8',errors='replace'))
                    result=subprocess.run([str(output/'Game.exe'),'--verify-package'],capture_output=True,timeout=60);self.assertEqual(result.returncode,0,result.stderr)
                finally:hidden.rename(store.Root)
