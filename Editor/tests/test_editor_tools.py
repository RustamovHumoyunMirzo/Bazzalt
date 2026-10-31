import os,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from PySide6.QtWidgets import QApplication
from Editor.macros import MacroStore
from Editor.runtime import _LoadNativeModule
from Editor.gui.application import Editor

class MacroStoreTests(unittest.TestCase):
    def test_snapshot_survives_source_deletion_and_validates_modules(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);entry=root/'Run.lua';module=root/'Helper.lua';entry.write_text('return function() end');module.write_text('return { value=3 }')
            store=MacroStore(root/'Library');data=store.Import('My macro',entry,[module]);entry.unlink();module.unlink()
            self.assertEqual(store.Load(data['id'])['modules']['Helper'],'return { value=3 }');self.assertEqual(store.List()[0]['entry'],'Run')
            store.Delete(data['id']);self.assertEqual(store.List(),[])
            with self.assertRaises(ValueError):store.Delete('../outside')
    def test_rejects_invalid_module_names(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'bad-name.lua';path.write_text('return {}')
            with self.assertRaises(ValueError):MacroStore(Path(directory)/'Library').Import('Invalid',path,[])

class FileUndoTests(unittest.TestCase):
    def test_file_edits_undo_redo_cancel_and_preserve_external_changes(self):
        from Editor.history import SceneHistory
        class Runtime:
            def CaptureScene(self):return b'scene'
            def RestoreScene(self,data):return True
            def RefreshAssets(self):pass
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Terrain.obj';history=SceneHistory(Runtime());history.Begin('Build terrain');history.RecordFile(path);path.write_bytes(b'new mesh');self.assertTrue(history.Commit())
            self.assertTrue(history.Undo());self.assertFalse(path.exists());self.assertTrue(history.Redo());self.assertEqual(path.read_bytes(),b'new mesh')
            path.write_bytes(b'external edit');self.assertFalse(history.Undo());self.assertEqual(path.read_bytes(),b'external edit')
            history.Begin('Cancel edit');history.RecordFile(path);path.write_bytes(b'bad mesh');history.Cancel();self.assertEqual(path.read_bytes(),b'external edit')

class EditorLuaTests(unittest.TestCase):
    def test_native_vm_is_editor_only_and_marshals_tables(self):
        native=_LoadNativeModule();self.assertIsNotNone(native);seen=[]
        def command(method,args):seen.append((method,args));return {'answer':42,'values':[1,2,3]}
        vm=native.EditorLua('assert(os==nil and io==nil and Bazzalt==nil and load==nil and debug==nil);local h=require("Helper");return {OnRun=function(self) local v=Editor.Echo({value=h.number});assert(v.answer==42 and v.values[3]==3);return true end}',{'Helper':'return {number=7}'},command,['Echo'])
        self.assertTrue(vm.call('OnRun',None));self.assertEqual(seen[0],('Echo',[{'value':7}]))
        hostile=native.EditorLua('return setmetatable({}, {__index=function() error("must not run") end})',{},command,[]);self.assertFalse(hostile.call('OnRun',None))
    def test_instruction_limit_and_missing_module_raise(self):
        native=_LoadNativeModule();vm=native.EditorLua('return function() while true do end end',{},lambda *_:None,[])
        with self.assertRaisesRegex(RuntimeError,'budget'):vm.call('OnRun',None)
        with self.assertRaisesRegex(RuntimeError,'stored editor bundle'):native.EditorLua('require("../outside");return {}',{},lambda *_:None,[])

class EditorToolUITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Temp=tempfile.TemporaryDirectory();self.Patch=patch('Editor.gui.panels.macros.MacroStore',lambda:MacroStore(Path(self.Temp.name)/'Macros'));self.Patch.start();self.Window=Editor()
    def tearDown(self):
        from PySide6.QtCore import QEvent
        self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete);self.Patch.stop();self.Temp.cleanup()
    def test_controls_reuse_shared_actions_and_macros_have_own_panel(self):
        w=self.Window;self.assertIn(w.MenuBar.IconsAction,w.Scene.ControlsMenu.actions());self.assertIn(w.MenuBar.GizmosAction,w.Scene.ControlsMenu.actions());self.assertIn('macros',[p.panel_id for p in w.Docking.panels()])
        action=w.EditorTools.AddControl('test.tool','Test tool','return {OnRun=function(self,e) Editor.Log(tostring(e.enabled)) end}')
        action.setChecked(True);self.assertIn('test.tool',w.EditorTools.Input);action.setChecked(False);self.assertNotIn('test.tool',w.EditorTools.Input)
    def test_tools_receive_viewport_and_programmatic_selection_changes(self):
        w=self.Window;entity=w.Runtime.CreateEntity('Selection target')
        source='return {OnRun=function() end,OnSelectionChanged=function(self,ids) self.last=ids[1] end,Check=function(self,id) assert(self.last==id);return true end}'
        action=w.EditorTools.AddControl('selection.tool','Selection tool',source);action.setChecked(True);w.Controller.SelectEntity(entity);w.EditorTools.PollSelection();self.assertTrue(w.EditorTools.Call('selection.tool','Check',entity))
    def test_tool_errors_roll_back_and_disable_the_control(self):
        w=self.Window;before=w.Runtime.EntityCount();action=w.EditorTools.AddControl('failing.tool','Failing tool','return function() Editor.CreateEntity("Must roll back");error("bad tool") end');action.setChecked(True)
        self.assertFalse(action.isChecked());self.assertEqual(w.Runtime.EntityCount(),before);self.assertNotIn('failing.tool',w.EditorTools.Sessions)
    def test_one_shot_extension_commands_do_not_toggle_input(self):
        w=self.Window;action=w.EditorTools.AddControl('command.tool','One shot','return function() Editor.Log("command") end',checkable=False);action.trigger();self.assertFalse(action.isCheckable());self.assertNotIn('command.tool',w.EditorTools.Input);self.assertIn('command.tool',w.EditorTools.Sessions)
    def test_simulated_pointer_mapping_uses_both_dimensions(self):
        from Editor.game_input import InputScale
        surface=self.Window.Output.Surface;surface.SimulatedSize=(390,844);surface.resize(323,700);sx,sy=InputScale(surface)
        self.assertAlmostEqual(sx*surface.width(),390);self.assertAlmostEqual(sy*surface.height(),844)
    def test_macro_dialog_ui_is_owned_and_can_build_programmatically(self):
        w=self.Window;source='return function() local d=Editor.CreateDialog("tools","My tools");local b=Editor.AddWidget(d,{id="run",type="button",text="Run"});assert(b=="tools/run");Editor.ShowDialog(d) end'
        w.EditorTools.Call('macro.test','OnRun',None,source)
        self.assertIn(('macro.test','tools'),w.EditorTools.Dialogs);self.assertEqual(w.EditorTools.Command('macro.test','GetValue',['tools/run']),'Run');w.EditorTools.Drop('macro.test');self.assertFalse(w.EditorTools.Dialogs)
    def test_macro_scene_edits_use_real_runtime_and_undo(self):
        w=self.Window
        if not w.Runtime.IsAvailable():self.skipTest('Native bridge unavailable')
        before=w.Runtime.EntityCount();source='return function() local id=Editor.CreateEntity("Macro object");Editor.AddComponent(id,"Primitive Object");Editor.Translate(id,{2,3,4}) end'
        w.EditorTools.Call('macro.edit','OnRun',None,source);self.assertIn('macro.edit',w.EditorTools.Sessions)
        self.assertEqual(w.Runtime.EntityCount(),before+1);entity=next(row for row in w.Runtime.Entities() if row['name']=='Macro object');self.assertEqual(tuple(w.Runtime.EntityDetails(entity['uuid'])['position']),(2,3,4))
        self.assertTrue(w.Controller.History.Undo());self.assertEqual(w.Runtime.EntityCount(),before)
    def test_pending_tool_edit_rolls_back_before_project_change(self):
        w=self.Window;before=w.Runtime.EntityCount()
        w.EditorTools.Call('macro.pending','OnRun',None,'return function() Editor.BeginUndo("Pending");Editor.CreateEntity("Temporary") end')
        self.assertEqual(w.Runtime.EntityCount(),before+1);w.Runtime.ProjectChanging.emit();self.assertEqual(w.Runtime.EntityCount(),before);self.assertIsNone(w.EditorTools.Owner)
    def test_output_camera_options_and_simulator_aspect(self):
        w=self.Window;self.assertEqual(w.Output.Camera.itemData(0),'');self.assertEqual(w.Output.Camera.itemData(1),'none');w.Output.Camera.setCurrentIndex(1);self.assertEqual(w.Output._no_camera.text(),'')
        w.Output.Device.setCurrentIndex(3);self.assertEqual(w.Output.Surface.SimulatedSize,(390,844));w.Output._game_container.resize(1000,700);w.Output._game_container.Fit();self.assertAlmostEqual(w.Output.Surface.width()/w.Output.Surface.height(),390/844,delta=.01)
