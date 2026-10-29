import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import tempfile,unittest,json
from pathlib import Path
from unittest.mock import Mock
from types import SimpleNamespace
from PySide6.QtWidgets import QApplication
from Editor.global_events import Read,Write,Validate
from Editor.scripting import ScriptCompiler
from Editor.subsystems import Registrations,KEY
class GlobalEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def test_schema_roundtrip_and_invalid_fields(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Signal.event';value=dict(Version=1,Name='Signal',Arguments=[dict(Name='Position',Type='Vec3')]);Write(path,value);self.assertEqual(Read(path),value)
            for bad in (dict(value,Version=True),dict(value,Name='bad name'),dict(value,Arguments=value['Arguments']*2),dict(value,Arguments=[dict(Name='A',Type='Unknown')])):
                with self.assertRaises(ValueError):Write(path,bad)
                self.assertEqual(Read(path),value)
    def test_schema_inspector_edits_and_play_lock(self):
        from Editor.gui.global_event import GlobalEventEditor
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Signal.event';Write(path,dict(Version=1,Name='Signal',Arguments=[]))
            runtime=Mock();runtime.IsPlaying.return_value=False;runtime.RefreshAsset.return_value=True
            controller=SimpleNamespace(Runtime=runtime,Window=SimpleNamespace(Localization=SimpleNamespace(Translate=lambda key:key),AssetBrowser=Mock()))
            widget=GlobalEventEditor(controller,path);widget.AddArgument(dict(Name='Value',Type='Number'));widget.Apply();self.assertEqual(Read(path)['Arguments'],[dict(Name='Value',Type='Number')]);runtime.RefreshAsset.assert_called_once_with(path)
            widget.AddArgument(dict(Name='Value',Type='String'));widget.Apply();self.assertTrue(widget.Error.text());self.assertEqual(len(Read(path)['Arguments']),1)
            runtime.IsPlaying.return_value=True;locked=GlobalEventEditor(controller,path);self.assertFalse(locked.isEnabled());widget.deleteLater();locked.deleteLater();self.App.processEvents()
    def test_script_reflection_and_uuid_property_wrapper(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Listen.cpp';path.write_text('#include <Bazzalt/GlobalEvent.h>\nCOMPONENT(Listen) { PROPERTY(Bazzalt::GlobalEvent, Signal, {}); };')
            descriptor=ScriptCompiler.Inspect(path);wrapper=ScriptCompiler._Wrapper(descriptor);self.assertIn('Bazzalt::GlobalEvent::Load(id)',wrapper);self.assertIn('#include <Bazzalt/GlobalEvent.h>',wrapper)
            from Editor.gui.controller import EditorController
            controller=SimpleNamespace(_ScriptDescriptor=lambda script:descriptor,_SetScriptProperty=Mock(),_AssetPicker=Mock())
            EditorController._RawScriptEditor(controller,{},'Signal','00000000-0000-0000-0000-000000000000');self.assertEqual(controller._AssetPicker.call_args.args[1],('.event',))
            path=Path(folder)/'Listen.lua';path.write_text('local C=COMPONENT("Listen")\nC.Signal=PROPERTY("GlobalEvent", Bazzalt.GlobalEvent.new())\nreturn C')
            self.assertEqual(ScriptCompiler.Inspect(path).properties[0].type,'GlobalEvent')
    def test_subsystem_permission_is_strict_and_backwards_compatible(self):
        row=dict(asset='12345678-1234-4234-8234-123456789abc',level=4)
        self.assertEqual(Registrations({KEY:json.dumps([row])}),[row])
        self.assertTrue(Registrations({KEY:json.dumps([dict(row,asset_access=True)])})[0]['asset_access'])
        for bad in (1,'true',None):
            with self.assertRaises(ValueError):Registrations({KEY:json.dumps([dict(row,asset_access=bad)])})

    def test_asset_browser_creates_and_selects_event(self):
        from Editor.gui.panels.assets import AssetBrowserPanel
        from Editor.localization import LocalizationManager
        from Editor.resources import ResourceManager
        with tempfile.TemporaryDirectory() as folder:
            resources=ResourceManager();locale=LocalizationManager(resources);panel=AssetBrowserPanel(locale,resources);panel.SetProjectRoot(folder);panel._Create('global_event');path=Path(folder)/'NewEvent.event';self.assertTrue(path.is_file());self.assertEqual(Read(path)['Name'],'NewEvent')
            self.assertEqual(Path(panel.Browser.currentItem().data(256)),path)
            panel.close();panel.deleteLater();self.App.processEvents()
