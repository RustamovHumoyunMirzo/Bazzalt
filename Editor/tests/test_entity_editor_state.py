import os
os.environ.setdefault("QT_QPA_PLATFORM","offscreen")
import unittest
from unittest.mock import patch
from PySide6.QtCore import Qt,QEvent
from PySide6.QtWidgets import QApplication,QLineEdit,QMenu
from Editor.gui.application import Editor


class EntityEditorStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.App=QApplication.instance() or QApplication([])
    def setUp(self):self.Window=Editor()
    def tearDown(self):
        self.Window.close();self.Window.deleteLater();self.App.sendPostedEvents(None,QEvent.Type.DeferredDelete)

    def test_scene_row_inspects_environment_without_add_component_and_survives_refresh(self):
        window=self.Window;scene=window.Hierarchy.Tree.topLevelItem(0)
        window.Hierarchy.Tree.setCurrentItem(scene)
        self.assertIn("scene.environment",window.Properties._sections)
        self.assertTrue(window.Properties.AddComponentButton.isHidden())
        window.Controller.RefreshHierarchy()
        self.assertEqual(window.Hierarchy.Tree.currentItem().data(0,Qt.ItemDataRole.UserRole),window.Runtime.SceneInfo()["uuid"])
        self.assertIn("scene.environment",window.Properties._sections)

    def test_lock_blocks_selection_edits_and_descendants_then_unlock_restores(self):
        w=self.Window;parent=w.Runtime.CreateEntity("Parent");child=w.Runtime.CreateEntity("Child",parent)
        w.Controller.SelectEntity(child);snapshot=w.Runtime.CaptureScene()
        w.Controller.SetEntityEditorState(parent,"locked",True)
        self.assertTrue(w.Runtime.IsEditorLocked(child));self.assertEqual(w.Controller.SelectedEntities,[])
        w.Controller.SelectEntity(child);self.assertFalse(w.Properties._sections)
        self.assertFalse(w.Runtime.Rename(child,"Changed"))
        self.assertFalse(w.Runtime.SetTransform(child,[1,2,3],[0,0,0,1],[1,1,1]))
        self.assertFalse(w.Runtime.AddComponent(child,"Camera"));self.assertFalse(w.Runtime.DestroyEntity(parent))
        self.assertEqual(snapshot,w.Runtime.CaptureScene())
        item=w.Hierarchy.Tree.topLevelItem(0).child(0)
        self.assertFalse(item.icon(1).isNull());self.assertFalse(item.flags()&Qt.ItemFlag.ItemIsSelectable)
        w.Controller.SetEntityEditorState(parent,"locked",False)
        self.assertTrue(w.Runtime.Rename(child,"Renamed"));w.Controller.SelectEntity(child)
        self.assertEqual(w.Controller.SelectedEntity,child)

    def test_visibility_is_editor_only_and_independent_of_lock(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Hidden");snapshot=w.Runtime.CaptureScene()
        w.Controller.SetEntityEditorState(entity,"hidden",True)
        self.assertTrue(w.Runtime.IsEditorHidden(entity));self.assertFalse(w.Runtime.IsEditorSelectable(entity))
        self.assertFalse(w.Runtime.IsEditorLocked(entity));self.assertEqual(snapshot,w.Runtime.CaptureScene())
        w.Controller.SelectSceneEntity(entity);self.assertEqual(w.Controller.SelectedEntity,"")
        w.Controller.SelectEntity(entity);self.assertEqual(w.Controller.SelectedEntity,entity)
        w.Controller.SetEntityEditorState(entity,"hidden",False);self.assertTrue(w.Runtime.IsEditorSelectable(entity))

    def test_component_search_filters_actions_and_keeps_existing_components_disabled(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Components");w.Controller.SelectEntity(entity)
        def inspect(menu,_position):
            search=menu.findChild(QLineEdit);self.assertIsNotNone(search)
            search.setText("camera")
            visible=[action.text() for action in menu.actions() if action.isVisible() and not action.isSeparator() and action.text()]
            self.assertEqual(visible,["Camera"])
            self.assertTrue(all(action.data()!="CameraRenderTarget" for action in menu.actions()))
            search.setText("");self.assertGreater(sum(action.isVisible() for action in menu.actions()),1)
        class InspectMenu(QMenu):
            def exec(self,position):return inspect(self,position)
        with patch("Editor.gui.controller.QMenu",InspectMenu):w.Controller.ShowAddComponentMenu()

    def test_project_scoped_editor_state_is_saved_without_dirty_scene(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Settings");saved=[];w._settings_saver=lambda value:saved.append(value)
        with patch.object(w.Runtime,"ProjectInfo",return_value={"uuid":"test-project"}):w.Controller.SetEntityEditorState(entity,"locked",True)
        self.assertTrue(saved);self.assertTrue(w._settings["entity_editor_state"]["test-project"][entity]["locked"])
        self.assertFalse(w.Controller.IsDirty)

    def test_lock_column_is_an_indicator_not_an_invisible_button(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Indicator");requests=[];w.Hierarchy.EditorStateRequested.connect(lambda *args:requests.append(args))
        item=w.Hierarchy.Tree.topLevelItem(0).child(0)
        self.assertTrue(item.icon(1).isNull());self.assertEqual(item.toolTip(1),"")
        w.Hierarchy._StatusClicked(item,1);self.assertEqual(requests,[]);self.assertFalse(w.Runtime.IsEditorLocked(entity))
        w.Controller.SetEntityEditorState(entity,"locked",True);item=w.Hierarchy.Tree.topLevelItem(0).child(0)
        self.assertFalse(item.icon(1).isNull());w.Hierarchy._StatusClicked(item,1)
        self.assertEqual(requests,[]);self.assertTrue(w.Runtime.IsEditorLocked(entity))

    def test_light_type_fields_units_and_invalid_values(self):
        w=self.Window;entity=w.Runtime.CreateEntity("Light test");self.assertTrue(w.Runtime.AddComponent(entity,"Light"));w.Controller.SelectEntity(entity)
        fields=w.Runtime.EntityDetails(entity)["component_data"]["Light"]
        self.assertIn("Range",fields);self.assertNotIn("Inner Cone",fields);self.assertNotIn("Sun Angular Radius",fields)
        w.Controller._CommitLightType(entity,1)
        fields=w.Runtime.EntityDetails(entity)["component_data"]["Light"]
        self.assertNotIn("Range",fields);self.assertAlmostEqual(fields["Sun Angular Radius"],.5357,places=3)
        self.assertTrue(w.Runtime.SetComponentProperty(entity,"Light","Sun Angular Radius",2.0))
        self.assertAlmostEqual(w.Runtime.EntityDetails(entity)["component_data"]["Light"]["Sun Angular Radius"],2,places=4)
        w.Controller._CommitLightType(entity,3)
        self.assertEqual(w.Controller.SelectedEntity,entity)
        self.assertTrue(w.Runtime.SetComponentProperty(entity,"Light","Outer Cone",10.0))
        fields=w.Runtime.EntityDetails(entity)["component_data"]["Light"]
        self.assertLessEqual(fields["Inner Cone"],fields["Outer Cone"]);self.assertIn("Range",fields);self.assertNotIn("Sun Halo Size",fields)
        self.assertFalse(w.Runtime.SetComponentProperty(entity,"Light","Range",-1.0))
        self.assertFalse(w.Runtime.SetComponentProperty(entity,"Light","Intensity",float("nan")))
        self.assertFalse(w.Runtime.SetComponentProperty(entity,"Light","Type",99))

if __name__=="__main__":unittest.main()
