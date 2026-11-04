import unittest
from pathlib import Path
from Editor.lighting_shader import InjectLighting
from Editor._lighting_sources import HELPER,REFLECTIONS

class LightingShaderTests(unittest.TestCase):
    def test_shared_sources_are_current(self):
        root=Path(__file__).resolve().parents[2]/'runtime/resources/shaders'
        self.assertEqual(HELPER,(root/'baked_lighting.glsl').read_text(encoding='utf-8'))
        self.assertEqual(REFLECTIONS,(root/'baked_reflections.glsl').read_text(encoding='utf-8'))
    def test_surface_wrapper_preserves_early_returns_and_user_reflection(self):
        source='material {shadingModel:lit,parameters:[{type:float4,name:tint}]} fragment {void material(inout MaterialInputs m){prepareMaterial(m);if(m.metallic>0.5)return;m.baseColor=materialParams.tint;}}'
        reflection={'parameters':[{'name':'tint'}]}
        lowered=InjectLighting(source,reflection)
        self.assertIn('void bazzaltUserMaterial(inout MaterialInputs m)',lowered)
        self.assertIn('bazzaltUserMaterial(material);bazzaltApplyDiffuse(material);',lowered)
        self.assertIn('name:bazzaltLightingData,precision:high',lowered)
        self.assertEqual(reflection,{'parameters':[{'name':'tint'}]})
        self.assertIn('bazzaltApplyDiffuse',InjectLighting('// shadingModel:unlit\n'+source,reflection))
    def test_unlit_postprocess_and_reserved_names(self):
        unlit='material {shadingModel:unlit} fragment {void material(inout MaterialInputs m){}}'
        self.assertEqual(InjectLighting(unlit,{'parameters':[]}),unlit)
        post='material {domain:postprocess} fragment {void postProcess(inout PostProcessInputs m){}}'
        self.assertEqual(InjectLighting(post,{'parameters':[]}),post)
        with self.assertRaises(ValueError):InjectLighting(unlit.replace('unlit','lit'),{'parameters':[{'name':'bazzaltLightingData'}]})
