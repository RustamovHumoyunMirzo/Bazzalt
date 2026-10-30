import json,tempfile,unittest
from pathlib import Path
from Editor.subsystems import Registrations,Sources,WithSubsystems,KEY
from Editor.scripting import ScriptCompiler,ScriptAttachments,ScriptValidationError

class SubsystemTests(unittest.TestCase):
    def test_modal_registry_groups_add_remove_and_save(self):
        from PySide6.QtWidgets import QApplication,QWidget
        from types import SimpleNamespace
        from unittest.mock import Mock
        from Editor.gui.global_subsystems import GlobalSubsystemsDialog
        app=QApplication.instance() or QApplication([])
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();assets=root/'Assets';assets.mkdir();project=root/'Game.bproject';project.write_text('AssetDirectory: Assets\n')
            source=assets/'Service.cpp';source.write_text('SUBSYSTEM(Service) { public: void OnRegister() override {} };')
            identity='12345678-1234-4234-8234-123456789abc';source.with_suffix('.cpp.meta').write_text('UUID: '+identity)
            record=dict(uuid=identity,path=str(source),name=source.name,extension='.cpp')
            parent=QWidget();parent.Localization=SimpleNamespace(Translate=lambda key:key);parent.Runtime=Mock();parent.Runtime.IsPlaying.return_value=False
            parent.Runtime.ResolveAssetRecord.side_effect=lambda value:record if value==identity else None
            parent.Runtime.ProjectInfo.return_value=dict(name='Game',project_path=str(project),properties={'custom':'keep'})
            parent.Runtime.UpdateProjectInfo.return_value=True;parent.Controller=SimpleNamespace(_ProjectAssets=lambda:[record])
            dialog=GlobalSubsystemsDialog(parent);self.assertTrue(dialog.isModal());self.assertEqual(dialog.Tree.topLevelItemCount(),14)
            dialog.Picker.SetValue(identity);dialog.Level.SetValue(2);dialog.AssetAccess.SetValue(True);dialog._Add();self.assertEqual(dialog.Values,[dict(asset=identity,level=2,asset_access=True)])
            dialog._Apply();properties=parent.Runtime.UpdateProjectInfo.call_args.args[1];self.assertEqual(properties['custom'],'keep');self.assertEqual(json.loads(properties[KEY]),dialog.Values)
            self.assertEqual(dialog.result(),dialog.DialogCode.Accepted);self.assertFalse(dialog.isVisible());self.assertFalse(dialog.Error.text())
            dialog.Tree.setCurrentItem(dialog.Tree.topLevelItem(2).child(0));dialog._Remove();self.assertEqual(dialog.Values,[])
            dialog.close();parent.close();app.processEvents()
    def test_registration_validation(self):
        identity='12345678-1234-4234-8234-123456789abc';entry=dict(asset=identity,level=4)
        self.assertEqual(Registrations({KEY:json.dumps([entry])}),[entry])
        for level in range(5,14):self.assertEqual(Registrations({KEY:json.dumps([dict(entry,level=level)])})[0]['level'],level)
        for values in ([entry,entry],[dict(entry,level=14)],[dict(entry,level=-1)],[dict(entry,level=True)],[dict(entry,asset='')]):
            with self.assertRaises(ValueError):Registrations({KEY:json.dumps(values)})
    def test_subsystem_wrapper_is_not_an_entity_wrapper(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();source=root/'Game.cpp';source.write_text('#include <Bazzalt/Subsystem.h>\nSUBSYSTEM(Game) { public: void OnRegister() override {} };')
            descriptor=ScriptCompiler.Inspect(source);self.assertTrue(descriptor.is_subsystem)
            wrapper=ScriptCompiler._Wrapper(descriptor);self.assertIn('BazzaltGetSubsystemModuleV1',wrapper);self.assertNotIn('BazzaltGetScriptModuleV1',wrapper)
            with self.assertRaises(ScriptValidationError):ScriptAttachments(root).Attach('entity',descriptor)
    def test_registered_component_source_cannot_also_be_attached(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();assets=root/'Assets';assets.mkdir();project=root/'Game.bproject';project.write_text('AssetDirectory: Assets\n')
            source=assets/'Game.cpp';source.write_text('COMPONENT(Game) { public: void OnRegister() {} };')
            identity='12345678-1234-4234-8234-123456789abc';source.with_suffix('.cpp.meta').write_text('UUID: '+identity)
            registrations=[dict(asset=identity,level=0)];sources=Sources(project,registrations)
            attachments=ScriptAttachments(root);global_scripts=WithSubsystems(attachments,sources)
            self.assertEqual(attachments.UsedSources(),[]);self.assertEqual(global_scripts.RuntimeBindings((root/'Game.dll',))[0]['entity'],'@subsystem:'+identity)
            project.write_text('AssetDirectory: Assets\nProperties:\n  application.subsystems: '+json.dumps(json.dumps(registrations))+'\n')
            with self.assertRaises(ScriptValidationError):attachments.Attach('entity',ScriptCompiler.Inspect(source))
            project.write_text('AssetDirectory: Assets\n');attachments.Attach('entity',ScriptCompiler.Inspect(source))
            with self.assertRaisesRegex(ValueError,'attached'):Sources(project,registrations)
    def test_private_absolute_and_escaping_includes_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();source=root/'Game.cpp';compiler=ScriptCompiler(root)
            for include in ('<Runtime/Engine.h>','"C:/private/Engine.h"','"../outside.h"'):
                source.write_text('#include '+include+'\n')
                with self.assertRaises(ScriptValidationError):compiler._ValidateIncludes(source)
            header=root/'Shared.h';header.write_text('#pragma once\n#include <Bazzalt/Math.h>\n');source.write_text('#include "Shared.h"\n')
            self.assertEqual(compiler._ValidateIncludes(source),{header})
