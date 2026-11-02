import unittest
from Editor.lod_shader import InjectCoverage
class LODShaderTests(unittest.TestCase):
    def test_private_uniform_keeps_reflection_and_handles_empty_parameters(self):
        reflection={'parameters':[]};source='material { parameters: [] } fragment {void material(inout MaterialInputs material){prepareMaterial(material);}}'
        lowered=InjectCoverage(source,reflection)
        self.assertEqual(reflection,{'parameters':[]});self.assertRegex(lowered,r'name: bazzaltLodFade }\s*\]');self.assertIn('discard;',lowered)
        self.assertNotIn('}, ]',lowered)
    def test_post_process_is_untouched_and_reserved_name_is_rejected(self):
        source='postProcess { postProcess.color = vec4(1.0); }';self.assertEqual(InjectCoverage(source,{'parameters':[]}),source)
        with self.assertRaisesRegex(ValueError,'Reserved'):
            InjectCoverage('material { } fragment {void material(inout MaterialInputs material){ prepareMaterial(material); }}',{'parameters':[{'name':'bazzaltLodFade'}]})

    def test_comments_cannot_claim_or_redirect_coverage(self):
        source='// prepareMaterial(material);\npostProcess { }'
        self.assertEqual(InjectCoverage(source,{'parameters':[]}),source)
        surface='// prepareMaterial(material);\nmaterial {} fragment {void material(inout MaterialInputs material){prepareMaterial(material);}}'
        result=InjectCoverage(surface,{'parameters':[]});self.assertTrue(result.startswith('// prepareMaterial(material);'))
        self.assertLess(result.index('void material'),result.index('if (materialParams.bazzaltLodFade'))
