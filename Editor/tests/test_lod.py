import os,unittest
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from unittest.mock import Mock
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QEvent
from Editor.gui.application import Editor
from Editor.theme import ThemeManager
class LODTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):
        self.Window=Editor(ThemeManager(self.App));self.R=self.Window.Runtime
        if not self.R.IsAvailable():self.skipTest('native runtime is not built')
    def tearDown(self):
        self.Window.Controller.SetDirty(False);self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    def test_editor_lod_fields_copy_and_validation(self):
        r=self.R;w=self.Window;group=r.CreateEntity('Group');high=r.CreateEntity('High',group);low=r.CreateEntity('Low',group);r.AddComponent(high,'Primitive Object');r.AddComponent(low,'Primitive Object');self.assertTrue(r.AddComponent(group,'LOD'))
        levels=[{'ScreenHeight':.5,'Roots':[high]},{'ScreenHeight':.1,'Roots':[low]}]
        self.assertTrue(r.SetComponentProperty(group,'LOD','Levels',levels));self.assertFalse(r.SetComponentProperty(group,'LOD','Levels',[dict(levels[0],Roots=[group])]))
        w.Controller.SelectEntity(group,force=True);section=w.Properties._sections['runtime.LOD'];widget=section._fields['Levels'];self.assertEqual(widget.GetValue()[1]['Roots'],[low]);self.assertEqual(section._fields['Forced Level'].GetValue(),-1)
        self.assertTrue(r.SetComponentProperty(group,'LOD','Forced Level',1));w.Controller.SelectEntity(group,force=True);w.Controller.DuplicateHierarchyEntities([group]);clone=w.Controller.SelectedEntity;data=r.EntityDetails(clone)['component_data']['LOD'];self.assertEqual(data['Forced Level'],1);self.assertNotEqual(data['Levels'][0]['Roots'],[high]);self.assertEqual(r.EntityDetails(data['Levels'][0]['Roots'][0])['parent'],clone)
        w.Controller.SelectEntity(group,force=True);widget=w.Properties._sections['runtime.LOD']._fields['Levels'];widget._AddLevel();self.assertEqual(len(r.EntityDetails(group)['component_data']['LOD']['Levels']),3);self.assertEqual(len(widget.GetValue()),3)
        w.Controller._CommitComponent(group,'LOD','Size',-1);self.assertGreater(r.EntityDetails(group)['component_data']['LOD']['Size'],0);self.assertGreater(w.Properties._sections['runtime.LOD']._fields['Size'].GetValue(),0)
    def test_inspector_elides_unchanged_writes_but_updates_live_values(self):
        r=self.R;w=self.Window;e=r.CreateEntity('Body');r.AddComponent(e,'Rigidbody');w.Controller.SelectEntity(e,force=True);field=w.Properties._sections['runtime.Rigidbody']._fields['Mass'];original=field.SetValue;field.SetValue=Mock(wraps=original)
        w.Controller._RefreshInspectorValues(preserve_editing=True);field.SetValue.reset_mock();w.Controller._RefreshInspectorValues(preserve_editing=True);field.SetValue.assert_not_called()
        self.assertTrue(r.SetComponentProperty(e,'Rigidbody','Mass',12.));w.Controller._RefreshInspectorValues(preserve_editing=True);field.SetValue.assert_called_once_with(12.);self.assertEqual(field.GetValue(),12.)
