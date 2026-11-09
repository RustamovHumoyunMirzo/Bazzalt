import unittest
from Editor.terrain_shader import TerrainVariant

class TerrainShaderTests(unittest.TestCase):
    def test_preserves_surface_program_and_injects_interpolated_coverage(self):
        source='material { shadingModel: unlit, requires:[uv0] } fragment { void material(inout MaterialInputs value) { value.baseColor=vec4(getUV0(),0.0,1.0); } }'
        result=TerrainVariant(source)
        self.assertIn('requires:[uv1,uv0]',result)
        self.assertIn('vec2 bazzaltRange = vec2(getUV1().x, 1.0 - getUV1().y)',result)
        self.assertIn('value.baseColor=vec4(getUV0(),0.0,1.0)',result)
        self.assertIn('discard;',result)
    def test_early_returns_and_derivatives_execute_before_coverage(self):
        source='material {} fragment {void material(inout MaterialInputs p){p.baseColor=vec4(fwidth(getUV0().x));return;}}'
        result=TerrainVariant(source)
        self.assertIn('void bazzaltTerrainMaterial(inout MaterialInputs p)',result)
        self.assertIn('bazzaltTerrainMaterial(p);',result)
        self.assertLess(result.index('fwidth('),result.index('vec2 bazzaltRange'))
    def test_existing_uv1_is_not_duplicated(self):
        source='material {requires: [ uv0, uv1 ]} fragment {void material(inout MaterialInputs material){}}'
        self.assertEqual(TerrainVariant(source).count('uv1'),1)
    def test_uv_flip_setting_is_respected(self):
        source='material {flipUV:false} fragment {void material(inout MaterialInputs m){}}'
        self.assertIn('vec2(getUV1().x, getUV1().y)',TerrainVariant(source))
    def test_post_processing_has_no_terrain_variant(self):
        self.assertIsNone(TerrainVariant('material {domain:postProcess} fragment {void postProcess(inout PostProcessInputs p){}}'))
