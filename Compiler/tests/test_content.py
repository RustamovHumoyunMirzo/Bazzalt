import copy,tempfile,unittest
from pathlib import Path
import yaml
from Compiler.content import FilterCooked
from Compiler.settings import DEFAULTS,Validate


class ContentFilterTests(unittest.TestCase):
    def test_application_entry_is_retained_and_cannot_be_excluded(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root)
            import json
            identity='12345678-1234-4234-8234-123456789abc'
            index=root/'.bazzalt/CookedAssets.yaml';data=yaml.safe_load(index.read_text());data['Assets'][-1]['UUID']=identity;index.write_text(yaml.safe_dump(data))
            (root/'Game.bproject').write_text('StartupScene: Assets/Main.bscene\nProperties:\n  application.subsystems: '+json.dumps(json.dumps([dict(asset=identity,level=4)]))+'\n')
            settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene'
            FilterCooked(root,settings);self.assertTrue((root/'Assets/Unused.bin').exists())
            settings['exclude_assets']='Assets/Unused.bin'
            with self.assertRaisesRegex(ValueError,'Subsystem'):FilterCooked(root,settings)
    def Fixture(self,root):
        records=[]
        for identity,name,contents in (('scene','Assets/Main.bscene',{'Mesh':'model'}),('model','Assets/Model.bin',None),('unused','Assets/Unused.bin',None)):
            source=root/name;cache=root/'.bazzalt/Cache'/identity/'current.bin';source.parent.mkdir(parents=True,exist_ok=True);cache.parent.mkdir(parents=True,exist_ok=True)
            payload=yaml.safe_dump(contents).encode() if contents else b'asset'
            source.write_bytes(payload);cache.write_bytes(payload);records.append(dict(UUID=identity,Source=name,Cache=cache.relative_to(root).as_posix(),Importer='Bazzalt.Raw'))
        (root/'Game.bproject').write_text('StartupScene: Assets/Main.bscene\n')
        (root/'.bazzalt/CookedAssets.yaml').write_text(yaml.safe_dump(dict(FormatVersion=1,Assets=records)))
    def test_stylesheet_references_keep_fonts_and_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root)
            (root/'Assets/Main.bscene').write_text('Stylesheet: style\n')
            index=root/'.bazzalt/CookedAssets.yaml';data=yaml.safe_load(index.read_text())
            for identity,name,importer in (('style','Assets/UI.css','Bazzalt.Stylesheet'),('font','Assets/UI.ttf','Bazzalt.Font')):
                source=root/name;source.write_bytes(b'content');cache=root/'.bazzalt/Cache'/identity/'current.bin';cache.parent.mkdir(parents=True);cache.write_bytes(b'cache')
                data['Assets'].append(dict(UUID=identity,Source=name,Cache=cache.relative_to(root).as_posix(),Importer=importer))
                if identity=='style':manifest=Path(str(cache)+'.dependencies.yml');manifest.write_text('- font\n')
            index.write_text(yaml.safe_dump(data));settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene';settings['exclude_assets']='Assets/UI.ttf'
            with self.assertRaisesRegex(ValueError,'required'):FilterCooked(root,settings)
            settings['exclude_assets']='';FilterCooked(root,settings)
            self.assertTrue((root/'Assets/UI.ttf').is_file());self.assertTrue(manifest.is_file())

    def test_retains_serialized_dependencies_and_strips_unselected_assets(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene'
            FilterCooked(root,settings)
            self.assertTrue((root/'Assets/Model.bin').exists());self.assertFalse((root/'Assets/Unused.bin').exists());self.assertFalse((root/'.bazzalt/Cache/unused/current.bin').exists())
            self.assertEqual(len(yaml.safe_load((root/'.bazzalt/CookedAssets.yaml').read_text())['Assets']),2)
    def test_excluding_required_dependency_fails_before_pruning(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);settings=copy.deepcopy(DEFAULTS['packaging']);settings['exclude_assets']='Assets/Model.bin'
            with self.assertRaisesRegex(ValueError,'required'):FilterCooked(root,settings)
            self.assertTrue((root/'Assets/Model.bin').exists())
            settings['exclude_assets']='Assets/Main.bscene'
            with self.assertRaisesRegex(ValueError,'Startup'):FilterCooked(root,settings)
    def test_unsafe_rules_fail_validation(self):
        for pattern in ('../Assets/*','C:/secret/*','/absolute/*','Assets\\*'):
            value=copy.deepcopy(DEFAULTS);value['packaging']['include_assets']=pattern
            with self.assertRaises(ValueError):Validate(value)
    def test_shader_reflection_dependencies_and_runtime_products_are_kept(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);source=root/'Assets/Effect.bshader';source.write_text('shader source');cache=root/'.bazzalt/Cache/shader/current.bshader';cache.parent.mkdir(parents=True);cache.write_text('shader source')
            for suffix,data in (('.reflection.json','{"Texture":"model"}'),('.filamat','compiled'),('.translated.mat','intermediate'),('.build.json','private tool paths')):Path(str(cache)+suffix).write_text(data)
            index=root/'.bazzalt/CookedAssets.yaml';values=yaml.safe_load(index.read_text());values['Assets'].append(dict(UUID='shader',Source='Assets/Effect.bshader',Cache=cache.relative_to(root).as_posix(),Importer='Bazzalt.FilamentMaterial'));index.write_text(yaml.safe_dump(values))
            (root/'Assets/Main.bscene').write_text('Shader: shader\n');settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene';FilterCooked(root,settings)
            self.assertTrue(Path(str(cache)+'.filamat').exists());self.assertTrue(Path(str(cache)+'.reflection.json').exists());self.assertTrue((root/'Assets/Model.bin').exists())
            self.assertFalse(Path(str(cache)+'.translated.mat').exists());self.assertFalse(Path(str(cache)+'.build.json').exists())
    def test_yaml_alias_cycles_do_not_hang_dependency_scan(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.Fixture(root);(root/'Assets/Main.bscene').write_text('loop: &self [*self, MODEL]\n')
            settings=copy.deepcopy(DEFAULTS['packaging']);settings['include_assets']='Assets/Main.bscene';FilterCooked(root,settings)
            self.assertTrue((root/'Assets/Model.bin').exists())
