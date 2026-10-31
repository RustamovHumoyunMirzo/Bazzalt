import copy,json,math,os,tempfile,unittest,uuid
from pathlib import Path
from PySide6.QtCore import QPointF
from PySide6.QtWidgets import QApplication
from Editor import shader_graph as G
from Editor.materials import MaterialCompiler,MaterialError
from Editor.resources import ResourceManager
from Editor.localization import LocalizationManager
from Editor.theme import ThemeManager
from Editor.gui.panels.shader_graph import ShaderGraphPanel

ROOT=Path(__file__).resolve().parents[2]
class Assets:
    def __init__(self,path):self.Path=path;self.Record={'uuid':str(uuid.uuid4()),'path':str(path),'cache':str(path.parent/'cache'/'graph.shadg')}
    def AssetInfo(self,_):return self.Record

class ShaderGraphTests(unittest.TestCase):
    def test_roundtrip_properties_and_shared_expression(self):
        graph=G.NewGraph();value=G.AddNode(graph,'Property');value['values']={'name':'Tint','kind':'float4','value':[.1,.2,.3,1.]};graph['nodes'][0]['inputs']['color']=value['id']
        source=G.Generate(graph);self.assertIn('Tint: vec4',source);self.assertIn('color = Tint',source)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'shader.shadg';G.Save(path,graph);self.assertEqual(G.Load(path),graph)
            meta=Path(str(path)+'.meta');meta.write_text('UUID: retained');target=G.Unlock(path)
            self.assertEqual(target.read_text(),source);self.assertFalse(path.exists());self.assertEqual(Path(str(target)+'.meta').read_text(),'UUID: retained')
            with self.assertRaises(MaterialError):G.Unlock(target.with_suffix('.shadg'))
    def test_rejects_cycles_missing_links_injection_and_vector_mismatch(self):
        graph=G.NewGraph();a=G.AddNode(graph,'Add');b=G.AddNode(graph,'Add');a['inputs']['a']=b['id'];b['inputs']['a']=a['id']
        with self.assertRaisesRegex(MaterialError,'cycle'):G.Generate(graph)
        b['inputs'].clear();a['inputs']['a']='missing'
        with self.assertRaisesRegex(MaterialError,'Missing'):G.Generate(graph)
        graph=G.NewGraph();n=G.AddNode(graph,'Property');n['values']['name']='bad;code';graph['nodes'][0]['inputs']['roughness']=n['id']
        with self.assertRaises(MaterialError):G.Generate(graph)
        n['type']='Vector3Value';graph['nodes'][0]['inputs']={'color':n['id']}
        with self.assertRaisesRegex(MaterialError,'Swizzle'):G.Generate(graph)
        n['type']='Float';n['values']={'value':float('nan')};graph['nodes'][0]['inputs']={'roughness':n['id']}
        with self.assertRaises(MaterialError):G.Generate(graph)
        graph=G.NewGraph();previous=None
        for _ in range(130):
            node=G.AddNode(graph,'Add')
            if previous:node['inputs']['a']=previous
            previous=node['id']
        with self.assertRaisesRegex(MaterialError,'depth'):G.Generate(graph)
    def test_texture_defaults_survive_source_conversion_and_custom_mesh_keeps_uvs(self):
        from Editor.shader_graph_preview import Geometry
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'Surface.shadg';graph=G.NewGraph();texture=G.AddNode(graph,'Texture');identity=str(uuid.uuid4());texture['values']={'name':'AlbedoMap','texture':identity};sample=G.AddNode(graph,'SampleTexture');sample['inputs']['texture']=texture['id'];graph['nodes'][0]['inputs']['color']=sample['id'];G.Save(path,graph)
            compiler=MaterialCompiler(Assets(path),ROOT)
            try:before=compiler.InspectShader('shader')
            except MaterialError as e:self.skipTest(str(e))
            target=G.Unlock(path);after=MaterialCompiler(Assets(target),ROOT).InspectShader('shader')
            self.assertEqual(before['parameters'][0]['default'],identity);self.assertEqual(after['parameters'][0]['default'],identity)
            mesh=root/'Triangle.obj';mesh.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nvt .2 .3\nvt .7 .3\nvt .2 .9\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n')
            vertices,indices=Geometry(mesh);self.assertEqual(len(indices),3);self.assertEqual(vertices[0][7:9],(.2,.3))
    def test_integer_uniforms_and_generated_names_do_not_collide(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Surface.shadg';graph=G.NewGraph();value=G.AddNode(graph,'Property');value['values']={'name':'g0','kind':'int','value':2};integer=G.AddNode(graph,'Integer');integer['values']['value']=3;product=G.AddNode(graph,'Multiply');product['inputs']={'a':value['id'],'b':integer['id']};graph['nodes'][0]['inputs']['roughness']=product['id'];G.Save(path,graph)
            compiler=MaterialCompiler(Assets(path),ROOT)
            try:reflection=compiler.CompileShader('shader')
            except MaterialError as error:
                if 'missing' in str(error):self.skipTest(str(error))
                raise
            self.assertEqual(reflection['parameters'][0]['kind'],4);self.assertEqual(reflection['parameters'][0]['default'],2)
    def test_each_catalog_node_compiles_with_real_shader_toolchain(self):
        compiler=MaterialCompiler(None,ROOT)
        try:compiler._Tool('matc');compiler._Translate(G.Generate(G.NewGraph()))
        except MaterialError as error:self.skipTest(str(error))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'graph.shadg';assets=Assets(path);compiler=MaterialCompiler(assets,ROOT)
            for name,spec in G.NODES.items():
                if name=='Material':continue
                with self.subTest(node=name):
                    graph=G.NewGraph();node=G.AddNode(graph,name);output=graph['nodes'][0]
                    if name=='Texture':
                        sample=G.AddNode(graph,'SampleTexture');sample['inputs']['texture']=node['id'];node=sample
                    elif name in ('SampleTexture','TextureLod'):node['inputs']['texture']=G.AddNode(graph,'Texture')['id']
                    kind=G.NODES[node['type']].Output
                    if kind=='property':kind='float'
                    if kind=='bool':
                        branch=G.AddNode(graph,'Branch');branch['inputs']['condition']=node['id'];node=branch;kind='float'
                    if kind=='float2':
                        swizzle=G.AddNode(graph,'Swizzle');swizzle['inputs']['x']=node['id'];node=swizzle;kind='float'
                    if name in G.VERTEX_ONLY:G.AddNode(graph,'Vertex')['inputs']['offset']=node['id']
                    elif name!='Vertex':output['inputs']['color' if kind=='float4' else 'normal' if kind=='float3' else 'roughness']=node['id']
                    G.Save(path,graph);compiler.CompileShader(assets.Record['uuid'])
                    self.assertTrue(Path(assets.Record['cache']+'.filamat').is_file())

class ShaderGraphWidgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_preview_loading_overlay_and_custom_toolbar_icons(self):
        resources=ResourceManager();panel=ShaderGraphPanel(None,LocalizationManager(resources),resources,ThemeManager(self.App))
        panel.Image.SetBusy(True);self.assertTrue(panel.Image.Busy);self.assertTrue(panel.Image.Spinner.isActive())
        panel.PreviewFailed('compile failed');self.assertFalse(panel.Image.Busy);self.assertFalse(panel.Image.Spinner.isActive());self.assertEqual(panel.Error.text(),'compile failed')
        for name in ('pin','undo','redo','frame','preview','refresh','unlock'):self.assertFalse(resources.Icon('icons/shader_graph/'+name+'.svg').isNull())
        panel.deleteLater();self.App.processEvents()
    def test_history_connection_deletion_and_unlock_restore_uuid(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Graph.shadg';G.Save(path,G.NewGraph());Path(str(path)+'.meta').write_text('UUID: retained')
            resources=ResourceManager();panel=ShaderGraphPanel(None,LocalizationManager(resources),resources,ThemeManager(self.App));panel.Preview=lambda:None
            self.assertTrue(panel.Open(path));panel.Add('Float',QPointF(10,10));identity=panel.Graph['nodes'][-1]['id'];panel.Nodes[identity].setSelected(True)
            from Editor.gui.widgets.fields import FloatInput
            field=panel.Fields.findChild(FloatInput);self.assertIsNotNone(field);field.setValue(2.5);self.assertTrue(panel.Nodes[identity].isSelected());self.assertEqual(G.Load(path)['nodes'][-1]['values']['value'],2.5)
            panel.Connect(identity,'output','roughness')
            self.assertEqual(G.Load(path)['nodes'][0]['inputs']['roughness'],identity)
            panel.History.undo();self.assertNotIn('roughness',panel.Graph['nodes'][0]['inputs']);panel.History.redo()
            panel.Unlock();self.assertTrue(path.with_suffix('.shad').is_file());panel.History.undo();self.assertTrue(path.is_file());self.assertEqual(Path(str(path)+'.meta').read_text(),'UUID: retained');panel.History.redo();self.assertTrue(path.with_suffix('.shad').is_file());panel.deleteLater();self.App.processEvents()
    def test_external_source_edits_are_never_overwritten_by_flush(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Graph.shadg';G.Save(path,G.NewGraph());resources=ResourceManager();panel=ShaderGraphPanel(None,LocalizationManager(resources),resources,ThemeManager(self.App));panel.Preview=lambda:None;panel.Open(path)
            external=G.NewGraph();external['name']='External';G.Save(path,external);panel.Graph['name']='PendingLocal';self.assertFalse(panel.Flush());self.assertEqual(G.Load(path)['name'],'External')
            panel.Reload();self.assertEqual(panel.Graph['name'],'External');panel.deleteLater();self.App.processEvents()
    def test_preview_publication_rejects_stale_sources_and_publishes_complete_products(self):
        from Editor.shader_graph_preview import ShaderGraphPreview
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Graph.shadg';G.Save(path,G.NewGraph());assets=Assets(path);runtime=SimpleNamespace(AssetInfo=assets.AssetInfo,SupportedRenderingBackends=lambda:['vulkan']);service=ShaderGraphPreview(runtime)
            source=path.read_bytes();products={'.filamat':b'complete package','.reflection.json':b'{}','.translated.mat':b'material {}','.build.json':b'{}'}
            path.write_text('external edit');self.assertFalse(service.Publish((assets.Record,source,products)));self.assertFalse(Path(assets.Record['cache']+'.filamat').exists())
            path.write_bytes(source);self.assertTrue(service.Publish((assets.Record,source,products)))
            for suffix,data in products.items():self.assertEqual(Path(assets.Record['cache']+suffix).read_bytes(),data)
            service.Close()
    @unittest.skipUnless(os.environ.get('BAZZALT_SHADER_PREVIEW_TEST')=='1','Explicit native GPU preview check')
    def test_actual_offscreen_material_sphere(self):
        from Editor.runtime import _LoadNativeModule
        from Editor.shader_graph_preview import Geometry
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'Graph.shadg';G.Save(path,G.NewGraph());assets=Assets(path);compiler=MaterialCompiler(assets,ROOT);compiler.CompileShader(assets.Record['uuid'])
            preview=_LoadNativeModule().ShaderPreview('metal' if __import__('sys').platform=='darwin' else 'vulkan');vertices,indices=Geometry();pixels=preview.render(Path(assets.Record['cache']+'.filamat').read_bytes(),vertices,indices,[],.4,.25,0.)
            self.assertEqual(len(pixels),256*256*4);self.assertNotEqual(pixels[(128*256+128)*4:(128*256+128)*4+3],pixels[:3])
            graph=G.NewGraph();graph['shading']='unlit';color=G.AddNode(graph,'VertexColor');graph['nodes'][0]['inputs']['color']=color['id'];G.Save(path,graph);compiler.CompileShader(assets.Record['uuid'])
            red=[tuple(row[:9])+(1.,0.,0.,1.) for row in vertices];colored=preview.render(Path(assets.Record['cache']+'.filamat').read_bytes(),red,indices,[],.4,.25,0.);center=(128*256+128)*4
            self.assertGreater(colored[center],colored[center+1]+20);self.assertGreater(colored[center],colored[center+2]+20)
            from Editor.shader_graph_preview import ShaderGraphPreview
            from PySide6.QtCore import QEventLoop,QTimer,QThread
            from types import SimpleNamespace
            binary=Path(assets.Record['cache']+'.filamat');binary.unlink();Path(assets.Record['cache']+'.reflection.json').unlink()
            runtime=SimpleNamespace(AssetInfo=assets.AssetInfo,SupportedRenderingBackends=lambda:['metal' if __import__('sys').platform=='darwin' else 'vulkan'])
            service=ShaderGraphPreview(runtime);loop=QEventLoop();results=[];errors=[];threads=[]
            service.Ready.connect(lambda image:(results.append(image),threads.append(QThread.currentThread()),loop.quit()));service.Failed.connect(lambda error:(errors.append(error),loop.quit()))
            timer=QTimer();timer.setSingleShot(True);timer.timeout.connect(loop.quit);timer.start(30000)
            try:
                service.Request(path);self.assertFalse(binary.exists());loop.exec();self.assertFalse(errors,errors);self.assertTrue(results,'Preview timed out');self.assertFalse(results[0].isNull());self.assertEqual(threads,[self.App.thread()]);self.assertTrue(binary.is_file());self.assertTrue(Path(assets.Record['cache']+'.reflection.json').is_file())
                # Another live engine must survive preview creation/destruction.
                again=preview.render(binary.read_bytes(),red,indices,[],.4,.25,0.);self.assertGreater(again[center],again[center+1]+20)
            finally:timer.stop();service.Close();service.Executor.shutdown(wait=True);del preview
