import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import uuid
from types import SimpleNamespace
from PySide6.QtCore import QObject
from PySide6.QtWidgets import QApplication
from Editor.gui.controller import EditorController
from Editor.gui.panels.properties import PropertiesPanel
from Editor.gui.widgets import AssetPickerInput,FloatInput,Vec4Input
from Editor.localization import LocalizationManager
from Editor.resources import ResourceManager

from Editor.materials import MaterialCompiler, MaterialError, Reflect, ZERO, ValidateValue

ROOT=Path(__file__).resolve().parents[2]

class Assets:
    def __init__(self,root):self.root=Path(root);self.assets={}
    def Add(self,name,text):
        path=self.root/name;path.write_text(text,encoding="utf-8");identity=str(uuid.uuid4())
        info={"uuid":identity,"path":str(path),"cache":str(self.root/"Cache"/(identity+path.suffix))};self.assets[identity]=info;self.assets[str(path)]=info;return identity
    def AssetInfo(self,value):return self.assets.get(str(value),{})
    def RefreshAssets(self):return True
    def AssetDirectory(self):return str(self.root)
    def LoadedScenes(self):return []
    def Entities(self):return []

class MaterialTests(unittest.TestCase):
    def test_reflect_typed_fields_ignores_fragment_and_comments(self):
        info=Reflect('''// material { fake }
        material { name:"Typed",parameters:[{type:float,name:roughness},{type:float4,name:tint},{type:bool,name:enabled},{type:int,name:count},{type:sampler2d,name:albedo}],requires:[uv0] }
        fragment { void material(inout MaterialInputs material) { } }''')
        self.assertEqual([p["kind"] for p in info["parameters"]],[0,3,5,4,6]);self.assertEqual(info["requires"],["uv0"])
    def test_invalid_values_do_not_cross_renderer_api(self):
        for p,v in (({"kind":0},float("nan")),({"kind":4},4294967295),({"kind":6},"not-a-uuid"),({"kind":5},1)):
            with self.assertRaises(MaterialError):ValidateValue(p,v)
    def test_shader_inspection_does_not_compile_unused_shader(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            assets=Assets(folder);shader=assets.Add("surface.mat",'material {name:"Surface",parameters:[{type:float,name:roughness}]} fragment {void material(inout MaterialInputs material){prepareMaterial(material);material.roughness=materialParams.roughness;}}')
            compiler=MaterialCompiler(assets,ROOT);compiler.InspectShader(shader)
            cache=Path(assets.AssetInfo(shader)["cache"])
            self.assertTrue(Path(str(cache)+".reflection.json").exists());self.assertFalse(Path(str(cache)+".filamat").exists())
    def test_matinst_persistence_and_demand_compilation_cache(self):
        if not (ROOT/"deps/filament/bin/matc.exe").exists():self.skipTest("SDK is unavailable")
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            assets=Assets(folder);shader=assets.Add("surface.mat",'material {name:"Surface",parameters:[{type:float,name:roughness}]} fragment {void material(inout MaterialInputs material){prepareMaterial(material);material.baseColor=vec4(1.0);material.roughness=materialParams.roughness;}}')
            material=assets.Add("surface.matinst",json.dumps({"version":1,"shader":shader,"properties":{"roughness":0.5}}))
            compiler=MaterialCompiler(assets,ROOT);compiler.PrepareMaterial(material)
            output=Path(assets.AssetInfo(shader)["cache"]+".filamat");self.assertTrue(output.exists());stamp=output.stat().st_mtime_ns
            compiler.PrepareMaterial(material);self.assertEqual(output.stat().st_mtime_ns,stamp)
            path=Path(assets.AssetInfo(material)["path"]);data=compiler.ReadMaterial(path);data["properties"]["roughness"]=0.9;compiler.SaveMaterial(path,data)
            self.assertEqual(compiler.ReadMaterial(path)["properties"]["roughness"],0.9);self.assertEqual(output.stat().st_mtime_ns,stamp)
    def test_bshader_translates_and_compiles_real_filament_package(self):
        if not (ROOT/"build/Editor/Release/bshad.dll").exists():self.skipTest("translator not built")
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            assets=Assets(folder);shader=assets.Add("surface.shad","shader Surface { properties { roughnessFactor:float=0.5; } material { color=vec4(1.0);roughness=roughnessFactor; } }")
            compiler=MaterialCompiler(assets,ROOT);info=compiler.CompileShader(shader)
            self.assertEqual(info["parameters"][0]["default"],0.5)
            self.assertTrue(Path(assets.AssetInfo(shader)["cache"]+".filamat").exists())
            source=Path(assets.AssetInfo(shader)["cache"]+".translated.mat").read_text()
            self.assertIn("materialParams.roughnessFactor",source);self.assertNotIn("default :",source)
    def test_bshader_texture_and_builtin_transform_compile(self):
        if not (ROOT/"build/Editor/Release/bshad.dll").exists():self.skipTest("translator not built")
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            assets=Assets(folder);shader=assets.Add("example.shad",(ROOT/"Bshader/examples/example.bshader").read_text())
            MaterialCompiler(assets,ROOT).CompileShader(shader)

    def test_material_inspector_has_typed_fields_and_saves_without_compiling_unused_shader(self):
        app=QApplication.instance() or QApplication([])
        with tempfile.TemporaryDirectory(dir=ROOT/"build") as folder:
            assets=Assets(folder);shader=assets.Add("typed.mat",'material {parameters:[{name:roughness,type:float},{name:tint,type:float4},{name:albedo,type:sampler2d}]}')
            material=assets.Add("typed.matinst",json.dumps({"version":1,"shader":shader,"properties":{}}));path=Path(assets.AssetInfo(material)["path"])
            for info in {a["uuid"]:a for a in assets.assets.values()}.values():Path(info["path"]+".meta").write_text("UUID: "+info["uuid"],encoding="utf-8")
            localization=LocalizationManager(ResourceManager());panel=PropertiesPanel(localization)
            controller=EditorController.__new__(EditorController);QObject.__init__(controller)
            controller.Runtime=assets;controller.MaterialCompiler=MaterialCompiler(assets,ROOT);controller.ScriptAttachments=None;controller._asset_database_dirty=False;controller._material_error=""
            errors=[];controller.Window=SimpleNamespace(Properties=panel,Localization=localization,Console=SimpleNamespace(AddMessage=lambda *args:errors.append(args)))
            controller._InspectMaterial(path)
            self.assertFalse(errors);fields=panel._sections["material"]._fields
            self.assertIsInstance(fields["Shader"],AssetPickerInput);self.assertIsInstance(fields["roughness"],FloatInput);self.assertIsInstance(fields["tint"],Vec4Input);self.assertIsInstance(fields["albedo"],AssetPickerInput)
            fields["roughness"].setValue(0.75);self.assertEqual(controller.MaterialCompiler.ReadMaterial(path)["properties"]["roughness"],0.75)
            self.assertFalse(Path(assets.AssetInfo(shader)["cache"]+".filamat").exists());panel.close()

if __name__=="__main__":unittest.main()
