"""Regression coverage for static API inventory and language boundaries."""
import unittest
from scripts.sync_api_reference import cpp_types, lua_types, updates, public_contract, closing, masked, lua_usage


class ApiReferenceTests(unittest.TestCase):
    def test_generated_contracts_are_current(self):
        self.assertEqual([str(path) for path,_ in updates()], [])

    def test_public_and_private_boundaries(self):
        types = cpp_types()
        self.assertNotIn('EntityServices',types)
        self.assertNotIn('MaterialServices',types)
        self.assertNotIn('InputState',types)
        self.assertNotIn('m_id',types['Texture'][2])
        self.assertIn('Resize',types['Texture'][2])
        self.assertIn('OnWindowResize',types['Application'][2])
        self.assertIn('OnBoot',types['Application'][2])
        self.assertIn('GetScene',types['Subsystem'][2])

    def test_lua_uses_actual_bindings(self):
        types=lua_types()
        for name in ('GUI','Frame','AudioSource','AudioMixer','Texture','ConsoleFilter','Application'):
            self.assertIn(name,types)
        self.assertNotIn('Subsystems',types)
        self.assertNotIn('ApplicationEvents',types)
        self.assertNotIn('OnBoot',types['Application'][1])
        self.assertIn('GetPlatform',types['Application'][1])
        self.assertIn('SetEffects',types['AudioBus'][1])
        self.assertIn('Volume',types['AudioBusSettings'][1])
        self.assertIn('FadeTo',types['AudioVoice'][1])
        self.assertIn('Length',types['Vec3'][1])

    def test_access_sections_templates_and_initializers(self):
        source='''class Sample {
        public:
            Vec2 Size{160,40};
            template<class T> T Get() const { return secret; }
            void Set(int value = 2) { secret=value; }
        private:
            int secret=0;
            void Hidden();
        };'''
        start=source.index('{')
        contract=public_contract(source,'class',start,closing(masked(source),start))
        self.assertIn('Vec2 Size{160,40};',contract)
        self.assertIn('T Get() const;',contract)
        self.assertIn('void Set(int value = 2);',contract)
        self.assertNotIn('secret',contract)
        self.assertNotIn('Hidden',contract)

    def test_lua_notation_precedes_native_adapter(self):
        usage=lua_usage('Application','SetTargetFrameRate',
            '[](sol::table,int value){Application::SetTargetFrameRate(value);}',
            ['static void SetTargetFrameRate(int framesPerSecond);'])
        self.assertIn('Bazzalt.Application.SetTargetFrameRate(value: number)',usage)
        self.assertNotIn('sol::table',usage)


if __name__=='__main__': unittest.main()
